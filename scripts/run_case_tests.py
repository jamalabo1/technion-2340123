#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PROMPT_TOKEN_RE = re.compile(r"^[A-Za-z0-9_./:-]+> ?")
DEFAULT_TIMEOUT_SECONDS = 5.0


@dataclass
class CaseResult:
    name: str
    passed: bool
    timed_out: bool
    returncode: int | None
    elapsed_seconds: float
    stdout_diff: str
    stderr_diff: str
    stdout: list[str]
    stderr: list[str]


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_cases_dir(root: Path) -> Path:
    tests_cases = root / "tests" / "cases"
    if tests_cases.exists():
        return tests_cases
    return root / "test" / "cases"


def default_binary(root: Path) -> Path | None:
    env_binary = os.environ.get("SMASH_TEST_BINARY")
    if env_binary:
        return Path(env_binary)

    candidates = [
        root / "cmake-build-debug-vm" / "skeleton_smash",
        root / "cmake-build-debug-wsl" / "skeleton_smash",
        root / "cmake-build-debug" / "skeleton_smash",
        root / "skeleton_smash",
        root / "smash",
    ]
    existing = [candidate for candidate in candidates if candidate.exists()]
    if existing:
        return max(existing, key=lambda candidate: candidate.stat().st_mtime)
    return None


def load_text_list(value: Any, field_name: str, path: Path) -> list[str]:
    if value is None:
        return []
    if isinstance(value, str):
        return value.splitlines()
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return value
    raise ValueError(f"{path}: `{field_name}` must be a string or a list of strings")


def strip_prompts(text: str) -> str:
    stripped_lines: list[str] = []
    for line in text.splitlines():
        previous = None
        while previous != line:
            previous = line
            line = PROMPT_TOKEN_RE.sub("", line)
        stripped_lines.append(line)
    return "\n".join(stripped_lines)


def normalize_lines(text: str, *, exact: bool) -> list[str]:
    if exact:
        return strip_prompts(text).splitlines()
    return [line.rstrip() for line in strip_prompts(text).splitlines() if line.strip()]


def expected_lines(lines: list[str], *, exact: bool) -> list[str]:
    if exact:
        return lines
    return [line.rstrip() for line in lines if line.strip()]


def unified_diff(expected: list[str], actual: list[str], label: str) -> str:
    if expected == actual:
        return ""
    return "\n".join(
        difflib.unified_diff(
            expected,
            actual,
            fromfile=f"expected {label}",
            tofile=f"actual {label}",
            lineterm="",
        )
    )


def terminate_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
            return
        except ProcessLookupError:
            return
        except OSError:
            pass
    process.kill()


def run_smash_case(
    binary: Path,
    commands: list[str],
    cwd: Path,
    timeout: float,
) -> tuple[str, str, int | None, bool, float]:
    input_commands = list(commands)
    if not input_commands or not input_commands[-1].strip().startswith("quit"):
        input_commands.append("quit")
    stdin_text = "\n".join(input_commands) + "\n"

    started_at = time.monotonic()
    process = subprocess.Popen(
        [str(binary)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(cwd),
        text=True,
        start_new_session=(os.name == "posix"),
    )

    timed_out = False
    try:
        stdout, stderr = process.communicate(stdin_text, timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        terminate_process(process)
        stdout, stderr = process.communicate()

    return stdout, stderr, process.returncode, timed_out, time.monotonic() - started_at


def run_case(
    path: Path,
    binary: Path,
    cases_dir: Path,
    cwd: Path,
    timeout: float,
    exact: bool,
) -> CaseResult:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    commands = load_text_list(data.get("input"), "input", path)
    expected_stdout = expected_lines(load_text_list(data.get("output"), "output", path), exact=exact)
    expected_stderr = expected_lines(load_text_list(data.get("stderr"), "stderr", path), exact=exact)

    stdout_text, stderr_text, returncode, timed_out, elapsed = run_smash_case(
        binary=binary,
        commands=commands,
        cwd=cwd,
        timeout=timeout,
    )

    actual_stdout = normalize_lines(stdout_text, exact=exact)
    actual_stderr = normalize_lines(stderr_text, exact=exact)
    stdout_diff = unified_diff(expected_stdout, actual_stdout, "stdout")
    stderr_diff = unified_diff(expected_stderr, actual_stderr, "stderr") if expected_stderr else ""
    passed = not timed_out and returncode == 0 and not stdout_diff and not stderr_diff

    return CaseResult(
        name=str(path.relative_to(cases_dir)),
        passed=passed,
        timed_out=timed_out,
        returncode=returncode,
        elapsed_seconds=elapsed,
        stdout_diff=stdout_diff,
        stderr_diff=stderr_diff,
        stdout=actual_stdout,
        stderr=actual_stderr,
    )


def discover_cases(cases_dir: Path, pattern: str) -> list[Path]:
    return sorted(path for path in cases_dir.rglob(pattern) if path.is_file())


def print_result(result: CaseResult, verbose: bool) -> None:
    status = "PASS" if result.passed else "FAIL"
    print(f"{status} {result.name} ({result.elapsed_seconds:.2f}s)")
    if result.passed and not verbose:
        return

    if result.timed_out:
        print("  timed out")
    if result.returncode != 0:
        print(f"  return code: {result.returncode}")
    if result.stdout_diff:
        print(result.stdout_diff)
    if result.stderr_diff:
        print(result.stderr_diff)
    if result.stderr and not result.stderr_diff:
        print("  stderr:")
        for line in result.stderr:
            print(f"    {line}")


def parse_args() -> argparse.Namespace:
    root = project_root()
    parser = argparse.ArgumentParser(
        description="Run JSON input/output cases from tests/cases against the smash executable."
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=default_binary(root),
        help="Path to the smash executable. Defaults to SMASH_TEST_BINARY or a known CMake build path.",
    )
    parser.add_argument(
        "--cases-dir",
        type=Path,
        default=default_cases_dir(root),
        help="Directory containing JSON cases. Defaults to tests/cases, then test/cases.",
    )
    parser.add_argument(
        "--workdir",
        type=Path,
        default=None,
        help="Directory to run smash in. Defaults to --cases-dir so helpers such as ./s resolve.",
    )
    parser.add_argument(
        "--pattern",
        default="*.json",
        help="Recursive glob pattern under --cases-dir.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Timeout in seconds for each case.",
    )
    parser.add_argument(
        "--exact",
        action="store_true",
        help="Compare blank lines and trailing spaces exactly after prompt removal.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print details for passing tests too.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.binary is None:
        print("No smash binary found. Build it first or pass --binary PATH.", file=sys.stderr)
        return 2

    binary = args.binary.resolve()
    cases_dir = args.cases_dir.resolve()
    workdir = args.workdir.resolve() if args.workdir is not None else cases_dir
    if not binary.exists():
        print(f"smash binary does not exist: {binary}", file=sys.stderr)
        return 2
    if not cases_dir.exists():
        print(f"case directory does not exist: {cases_dir}", file=sys.stderr)
        return 2
    if not workdir.exists():
        print(f"working directory does not exist: {workdir}", file=sys.stderr)
        return 2

    cases = discover_cases(cases_dir, args.pattern)
    if not cases:
        print(f"no cases matched {args.pattern!r} under {cases_dir}", file=sys.stderr)
        return 2

    print(f"binary: {binary}")
    print(f"cases:  {cases_dir}")
    print(f"workdir: {workdir}\n")

    results = [
        run_case(
            path=case,
            binary=binary,
            cases_dir=cases_dir,
            cwd=workdir,
            timeout=args.timeout,
            exact=args.exact,
        )
        for case in cases
    ]

    for result in results:
        print_result(result, args.verbose)

    passed = sum(1 for result in results if result.passed)
    failed = len(results) - passed
    print(f"\n{passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
