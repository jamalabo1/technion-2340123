from __future__ import annotations

import re
from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, create_executable_script, parse_jobs


def test_redirect_overwrites_appends_and_supports_spacing_variants(run, tmp_workdir: Path):
    out = tmp_workdir / "out.txt"
    out.write_text("old\n")

    result = run(
        [
            "/bin/echo first>out.txt",
            "/bin/echo second >> out.txt",
            "/bin/echo third>>out.txt",
            "cat out.txt",
            "quit",
        ],
        cwd=tmp_workdir,
    )
    result.assert_exit_ok()
    assert out.read_text() == "first\nsecond\nthird\n"
    assert result.stdout_lines() == ["first", "second", "third"]
    assert_stderr_empty(result)


def test_redirect_builtin_and_external_then_restores_stdout(run, tmp_workdir: Path):
    result = run(["pwd > pwd.txt", "showpid > pid.txt", "pwd", "cat pwd.txt", "cat pid.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert lines[0] == str(tmp_workdir)
    assert lines[1] == str(tmp_workdir)
    assert re.match(r"smash pid is \d+", lines[2])
    assert (tmp_workdir / "pwd.txt").read_text().strip() == str(tmp_workdir)
    assert re.match(r"smash pid is \d+\n?", (tmp_workdir / "pid.txt").read_text())
    assert_stderr_empty(result)


def test_redirect_does_not_redirect_stderr(run, tmp_workdir: Path):
    result = run(["cd missing > err.txt", "cat err.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert (tmp_workdir / "err.txt").read_text() == ""
    assert result.stdout_lines() == []
    assert_stderr_contains(result, "smash error: chdir failed")


def test_redirect_ignores_background_marker_and_does_not_create_job(run, tmp_workdir: Path):
    result = run(["showpid > pid.txt &", "jobs", "cat pid.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == []
    assert re.match(r"smash pid is \d+", result.stdout_lines()[0])
    assert_stderr_empty(result)


def test_redirect_with_alias_expansion(run, tmp_workdir: Path):
    result = run(["alias p='pwd'", "p>alias_pwd.txt", "cat alias_pwd.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(tmp_workdir)]
    assert (tmp_workdir / "alias_pwd.txt").read_text().strip() == str(tmp_workdir)
    assert_stderr_empty(result)


def test_redirect_open_failure_uses_perror_and_stdout_is_restored(run, tmp_workdir: Path):
    result = run(["/bin/echo no > missing_dir/out.txt", "/bin/echo after", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: open failed")
    assert result.stdout_lines() == ["after"]
