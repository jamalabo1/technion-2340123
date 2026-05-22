from __future__ import annotations

import os
import re
from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, parse_pid_from_showpid


def test_initial_prompt_and_empty_commands(run):
    result = run(["", "   ", "quit"])
    result.assert_exit_ok()
    assert result.stdout.startswith("smash> ")
    assert result.stdout.count("smash> ") >= 3
    assert_stderr_empty(result)


def test_commands_accept_leading_and_repeated_spaces(run, tmp_workdir: Path):
    result = run(["     pwd", "showpid      ignored     args", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert str(tmp_workdir) in lines
    assert re.search(r"smash pid is \d+", result.stdout)
    assert_stderr_empty(result)


def test_chprompt_changes_resets_and_uses_only_first_argument(run):
    result = run(["chprompt alpha", "chprompt beta ignored", "chprompt", "quit"])
    result.assert_exit_ok()
    assert "alpha> " in result.stdout
    assert "beta> " in result.stdout
    assert "ignored> " not in result.stdout
    assert result.stdout.rstrip().endswith("smash>")
    assert_stderr_empty(result)


def test_chprompt_does_not_change_error_prefix_or_showpid(run):
    result = run(["chprompt custom", "showpid", "cd a b", "quit"])
    result.assert_exit_ok()
    assert re.search(r"smash pid is \d+", result.stdout)
    assert "custom error:" not in result.stderr
    assert_stderr_contains(result, "smash error: cd: too many arguments")


def test_showpid_reports_running_smash_pid_and_ignores_background_marker(smash_session):
    session = smash_session()
    session.read_until_prompt()
    session.send_line("showpid &")
    session.read_until_prompt()
    session.send_line("quit")
    result = session.finish()
    result.assert_exit_ok()
    assert parse_pid_from_showpid(result.stdout) == session.pid
    assert_stderr_empty(result)


def test_pwd_ignores_extra_args_reflects_cd_and_ignores_background_marker(run, tmp_workdir: Path):
    child = tmp_workdir / "child"
    child.mkdir()
    result = run(["pwd extra args", "cd child", "pwd &", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert lines[0] == str(tmp_workdir)
    assert lines[1] == str(child)
    assert_stderr_empty(result)


def test_whoami_prints_passwd_entry_for_current_uid(run):
    passwd_line = next(
        line for line in Path("/etc/passwd").read_text().splitlines()
        if int(line.split(":")[2]) == os.getuid()
    )
    fields = passwd_line.split(":")
    expected = [fields[0], str(os.getuid()), str(os.getgid()), fields[5]]

    result = run(["whoami ignored args", "quit"])
    result.assert_exit_ok()
    assert result.stdout_lines() == expected
    assert_stderr_empty(result)
