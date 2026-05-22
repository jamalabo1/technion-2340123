from __future__ import annotations

import os
import re
import signal
import stat
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional, Sequence


PROMPT_TOKEN_RE = re.compile(r"^[A-Za-z0-9_./:-]+> ?")
JOB_RE = re.compile(r"^\[(?P<id>\d+)\]\s+(?P<command>.+)$")


@dataclass
class SmashRunResult:
    stdout: str
    stderr: str
    returncode: Optional[int]
    timed_out: bool
    elapsed_seconds: float

    def assert_no_timeout(self) -> None:
        assert not self.timed_out, (
            "smash timed out\n"
            f"stdout:\n{self.stdout}\n"
            f"stderr:\n{self.stderr}"
        )

    def assert_exit_ok(self) -> None:
        self.assert_no_timeout()
        assert self.returncode == 0, (
            f"smash exited with {self.returncode}\n"
            f"stdout:\n{self.stdout}\n"
            f"stderr:\n{self.stderr}"
        )

    def clean_stdout(self) -> str:
        return strip_prompts(self.stdout).strip()

    def stdout_lines(self) -> list[str]:
        return [line.rstrip() for line in self.clean_stdout().splitlines() if line.strip()]

    def stderr_lines(self) -> list[str]:
        return [line.rstrip() for line in self.stderr.splitlines() if line.strip()]


class SmashSession:
    DEFAULT_TIMEOUT = 5.0

    def __init__(
        self,
        binary: str | Path,
        cwd: str | Path | None = None,
        env: dict[str, str] | None = None,
    ) -> None:
        if sys.platform != "linux":
            raise RuntimeError("smash integration tests require Linux")

        self.binary = str(binary)
        self.cwd = str(cwd) if cwd is not None else None
        self.env = {**os.environ, **(env or {})}
        self.started_at = time.monotonic()
        self._stdout = bytearray()
        self._stderr = bytearray()
        self._stdin_closed = False

        self.process = subprocess.Popen(
            [self.binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=self.cwd,
            env=self.env,
            preexec_fn=os.setsid,
        )
        self._proc = self.process

        assert self.process.stdout is not None
        assert self.process.stderr is not None
        os.set_blocking(self.process.stdout.fileno(), False)
        os.set_blocking(self.process.stderr.fileno(), False)

    @property
    def pid(self) -> int:
        return self.process.pid

    @property
    def stdout(self) -> str:
        self._read_available()
        return self._stdout.decode(errors="replace")

    @property
    def stderr(self) -> str:
        self._read_available()
        return self._stderr.decode(errors="replace")

    def send_line(self, command: str) -> None:
        assert self.process.stdin is not None
        if self._stdin_closed:
            raise RuntimeError("stdin is closed")
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()

    # Compatibility with some older tests.
    send = send_line

    def run_commands(self, commands: Sequence[str], timeout: float = DEFAULT_TIMEOUT) -> SmashRunResult:
        for command in commands:
            self.send_line(command)
        return self.finish(timeout=timeout)

    def read_until_prompt(self, prompt: str = "smash", timeout: float = DEFAULT_TIMEOUT) -> str:
        needle = f"{prompt}> "
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._read_available()
            out = self._stdout.decode(errors="replace")
            if out.endswith(needle):
                return out
            if self.process.poll() is not None:
                self._read_available()
                break
            time.sleep(0.01)
        raise TimeoutError(
            f"timed out waiting for prompt {needle!r}\n"
            f"stdout:\n{self.stdout}\n"
            f"stderr:\n{self.stderr}"
        )

    def send_signal(self, sig: int) -> None:
        self.process.send_signal(sig)

    def finish(self, timeout: float = DEFAULT_TIMEOUT) -> SmashRunResult:
        if self.process.stdin is not None and not self._stdin_closed:
            try:
                self.process.stdin.close()
            except OSError:
                pass
            self._stdin_closed = True

        timed_out = False
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._read_available()
            if self.process.poll() is not None:
                break
            time.sleep(0.01)

        if self.process.poll() is None:
            timed_out = True
            self.kill_process_group()
        self._read_available()

        return SmashRunResult(
            stdout=self._stdout.decode(errors="replace"),
            stderr=self._stderr.decode(errors="replace"),
            returncode=self.process.returncode,
            timed_out=timed_out,
            elapsed_seconds=time.monotonic() - self.started_at,
        )

    def terminate(self) -> None:
        if self.process.poll() is not None:
            self._read_available()
            return
        try:
            self.send_line("quit kill")
            self.finish(timeout=1)
        except Exception:
            self.kill_process_group()

    def kill_process_group(self) -> None:
        try:
            os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass
        except OSError:
            try:
                self.process.kill()
            except OSError:
                pass
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            try:
                self.process.kill()
            except OSError:
                pass
            self.process.wait(timeout=2)
        self._read_available()

    # Compatibility with old fixture teardown.
    _cleanup = kill_process_group

    def _read_available(self) -> None:
        for pipe, target in ((self.process.stdout, self._stdout), (self.process.stderr, self._stderr)):
            if pipe is None:
                continue
            fd = pipe.fileno()
            while True:
                try:
                    chunk = os.read(fd, 65536)
                except BlockingIOError:
                    break
                except OSError:
                    break
                if not chunk:
                    break
                target.extend(chunk)

    def __enter__(self) -> "SmashSession":
        return self

    def __exit__(self, *exc: object) -> None:
        self.terminate()


def run_smash(
    binary: str | Path,
    commands: Sequence[str],
    cwd: str | Path | None = None,
    timeout: float = SmashSession.DEFAULT_TIMEOUT,
    env: dict[str, str] | None = None,
) -> SmashRunResult:
    with SmashSession(binary=binary, cwd=cwd, env=env) as session:
        return session.run_commands(commands, timeout=timeout)


def wait_for_condition(predicate: Callable[[], bool], timeout: float = 2.0, interval: float = 0.02) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


def assert_stdout_contains(result: SmashRunResult, expected: str) -> None:
    assert expected in result.stdout, (
        f"expected stdout to contain {expected!r}\n"
        f"stdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )


def assert_stderr_contains(result: SmashRunResult, expected: str) -> None:
    assert expected in result.stderr, (
        f"expected stderr to contain {expected!r}\n"
        f"stdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )


def assert_stderr_empty(result: SmashRunResult) -> None:
    assert result.stderr == "", f"unexpected stderr:\n{result.stderr}\nstdout:\n{result.stdout}"


def parse_jobs(output: str) -> list[tuple[int, str]]:
    clean = strip_prompts(output)
    jobs: list[tuple[int, str]] = []
    for line in clean.splitlines():
        match = JOB_RE.match(line.strip())
        if match:
            jobs.append((int(match.group("id")), match.group("command")))
    return jobs


def strip_prompts(text: str) -> str:
    stripped_lines: list[str] = []
    for line in text.splitlines():
        previous = None
        while previous != line:
            previous = line
            line = PROMPT_TOKEN_RE.sub("", line)
        stripped_lines.append(line)
    return "\n".join(stripped_lines)


def parse_pid_from_showpid(output: str) -> int:
    match = re.search(r"smash pid is (\d+)", output)
    assert match, f"showpid output not found in:\n{output}"
    return int(match.group(1))


def create_executable_script(path: Path, body: str) -> Path:
    path.write_text(body)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


def expected_du_kb(path: Path) -> int:
    total_bytes = 0

    def visit(p: Path) -> None:
        nonlocal total_bytes
        st = os.lstat(p)
        total_bytes += st.st_blocks * 512
        if stat.S_ISDIR(st.st_mode):
            for child in p.iterdir():
                visit(child)

    visit(path)
    return (total_bytes + 1023) // 1024


def pid_exists(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def process_cmdline(pid: int) -> str:
    try:
        data = Path(f"/proc/{pid}/cmdline").read_bytes()
    except FileNotFoundError:
        return ""
    return data.replace(b"\x00", b" ").decode(errors="replace").strip()


def child_pids(parent_pid: int) -> list[int]:
    children: list[int] = []
    for proc_dir in Path("/proc").iterdir():
        if not proc_dir.name.isdigit():
            continue
        try:
            stat_text = (proc_dir / "stat").read_text()
        except (FileNotFoundError, PermissionError):
            continue
        fields = stat_text.split()
        if len(fields) > 3 and fields[3] == str(parent_pid):
            children.append(int(proc_dir.name))
    return children


def kill_pids(pids: Iterable[int]) -> None:
    for pid in pids:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
