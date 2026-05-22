from __future__ import annotations

import os
import time
from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, create_executable_script, parse_jobs


def test_simple_external_executes_without_shell_and_passes_arguments(run, tmp_workdir: Path):
    script = create_executable_script(
        tmp_workdir / "argv.sh",
        "#!/bin/sh\nprintf '%s\\n' \"$0\" \"$1\" \"$2\" \"$3\"\n",
    )
    result = run([f"{script} alpha beta gamma", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(script), "alpha", "beta", "gamma"]
    assert_stderr_empty(result)


def test_path_lookup_uses_smash_environment(run, tmp_workdir: Path):
    bin_dir = tmp_workdir / "bin"
    bin_dir.mkdir()
    create_executable_script(bin_dir / "hello-path", "#!/bin/sh\necho from_path\n")
    env = {"PATH": f"{bin_dir}:{os.environ.get('PATH', '')}"}

    result = run(["hello-path", "quit"], cwd=tmp_workdir, env=env)
    result.assert_exit_ok()
    assert result.stdout_lines() == ["from_path"]
    assert_stderr_empty(result)


def test_nonexistent_external_command_reports_execvp_perror_on_stderr(run):
    result = run(["definitely_no_such_smash_command_928374", "quit"])
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: execvp failed")
    assert "execvp failed" not in result.stdout


def test_complex_external_with_wildcards_runs_through_bash(run, tmp_workdir: Path):
    (tmp_workdir / "a.txt").write_text("")
    (tmp_workdir / "b.txt").write_text("")
    (tmp_workdir / "x.log").write_text("")

    result = run(["/bin/echo *.txt", "/bin/echo ?.log", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert set(lines[0].split()) == {"a.txt", "b.txt"}
    assert lines[1] == "x.log"
    assert_stderr_empty(result)


def test_foreground_external_waits_before_next_command(run):
    start = time.monotonic()
    result = run(["/bin/sleep 0.25", "/bin/echo after", "quit"], timeout=3)
    elapsed = time.monotonic() - start
    result.assert_exit_ok()
    assert elapsed >= 0.20
    assert result.stdout_lines() == ["after"]
    assert_stderr_empty(result)


def test_background_external_returns_prompt_and_is_listed(run, tmp_workdir: Path):
    result = run(["sleep 30&", "pwd", "jobs", "quit kill"], cwd=tmp_workdir, timeout=6)
    result.assert_exit_ok()
    assert str(tmp_workdir) in result.stdout_lines()
    assert parse_jobs(result.stdout) == [(1, "sleep 30&")]
    assert_stderr_empty(result)
