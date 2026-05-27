from __future__ import annotations

from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, create_executable_script, parse_pid_from_showpid


def test_stdout_pipe_external_to_external(run):
    result = run(["/bin/echo needle hay | grep needle", "quit"])
    result.assert_exit_ok()
    assert result.stdout_lines() == ["needle hay"]
    assert_stderr_empty(result)


def test_stdout_pipe_builtin_to_external(run, tmp_workdir: Path):
    result = run([f"pwd | grep {tmp_workdir.name}", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(tmp_workdir)]
    assert_stderr_empty(result)


def test_stdout_pipe_external_to_builtin_runs_builtin(run, tmp_workdir: Path):
    result = run(["/bin/echo ignored | pwd", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(tmp_workdir)]
    assert_stderr_empty(result)


def test_stdout_pipe_ignores_bg(run, tmp_workdir: Path):
    result = run(["echo ignored& | cat", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == ["ignored"]
    assert_stderr_empty(result)


def test_stdout_pipe_ignores_blocking_right_bg(run, tmp_workdir: Path):
    # exploiting the fact that ? runs on bash
    result = run([
        "(sleep 2 && echo test?)&& | cat", "quit"],
        cwd=tmp_workdir,
        timeout=3
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["test?"]
    assert_stderr_empty(result)


def test_stdout_pipe_ignores_blocking_left_bg(run, tmp_workdir: Path):
    # exploiting the fact that ? runs on bash
    result = run([
        "(sleep 2 && echo test?) | cat&&", "quit"],
        cwd=tmp_workdir,
        timeout=3
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["test?"]
    assert_stderr_empty(result)

def test_stdout_pipe_ignores_blocking_bg(run, tmp_workdir: Path):
    # exploiting the fact that ? runs on bash
    result = run([
        "(sleep 2 && echo test?)& | cat&&", "quit"],
        cwd=tmp_workdir,
        timeout=3
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["test?"]
    assert_stderr_empty(result)


def test_stderr_pipe_pipes_only_stderr_to_right_command(run, tmp_workdir: Path):
    script = create_executable_script(
        tmp_workdir / "both.sh",
        "#!/bin/sh\necho STDOUT_LINE\necho STDERR_LINE >&2\n",
    )
    result = run([f"{script} |& grep STDERR", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == ["STDOUT_LINE", "STDERR_LINE"]
    assert_stderr_empty(result)


def test_pipe_with_alias_expansion_only_on_left(run, tmp_workdir: Path):
    txt = "needle\nother\n"
    (tmp_workdir / "data.txt").write_text(txt)
    result = run(
        [
            "alias c='cat data.txt'",
            "alias cat='grep needle'",
            "c | cat",
            "quit",
        ],
        cwd=tmp_workdir,
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == txt.split("\n")[:-1]
    assert_stderr_empty(result)


def test_pipe_exec_failure_stays_on_stderr(run):
    result = run(["definitely_missing_smash_pipe_cmd | cat", "quit"])
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: execvp failed")
    assert "execvp failed" not in result.stdout

def test_pipeline_runs_builtin_on_parent(smash_session, tmp_workdir: Path):
    # Requirement: built-ins execute in smash, ignore &, and must not be inserted
    # into the jobs list.
    session = smash_session(cwd=tmp_workdir)
    session.read_until_prompt()
    session.send_line("showpid | cat")
    session.read_until_prompt()
    session.send_line("quit")
    result = session.finish()

    result.assert_exit_ok()
    assert parse_pid_from_showpid(result.stdout) == session.pid
    assert result.stderr == ""