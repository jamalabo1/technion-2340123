from __future__ import annotations

import re
import signal
from pathlib import Path

from .smash_harness import (
    assert_stderr_contains,
    child_pids,
    create_executable_script,
    parse_jobs,
    parse_pid_from_showpid,
    wait_for_condition,
)


def _assert_no_crlf(result) -> None:
    assert "\r" not in result.stdout, f"stdout contains CR bytes:\n{result.stdout!r}"
    assert "\r" not in result.stderr, f"stderr contains CR bytes:\n{result.stderr!r}"


def test_whitespace_macro_characters_split_arguments_and_20_arg_boundary(run, tmp_workdir: Path):
    # Requirement: WHITESPACE macro characters separate words, and up to 20 args
    # including the command name are accepted.
    create_executable_script(
        tmp_workdir / "argv",
        "#!/bin/sh\nprintf '<%s>\\n' \"$@\"\n",
    )
    args = [f"a{i}" for i in range(1, 20)]
    command = "./argv\t" + "\f".join(args[:7]) + "\v" + "\t".join(args[7:13]) + "  " + "  ".join(args[13:])

    result = run([command, "quit"], cwd=tmp_workdir)

    result.assert_exit_ok()
    assert result.stdout_lines() == [f"<{arg}>" for arg in args]
    assert result.stderr == ""
    _assert_no_crlf(result)


def test_prompt_error_channels_and_line_endings_are_exact(run):
    # Requirement: prompt changes do not affect fixed error/showpid prefixes, and
    # output must not hide CRLF or channel mistakes.
    result = run(["chprompt exact", "cd missing", "showpid", "chprompt", "quit"])

    result.assert_exit_ok()
    assert re.fullmatch(r"smash> exact> exact> smash pid is \d+\nexact> smash> ", result.stdout)
    assert result.stderr == "smash error: chdir failed: No such file or directory\n"
    _assert_no_crlf(result)


def test_cd_error_precedence_and_failed_cd_preserves_oldpwd(run, tmp_workdir: Path):
    # Requirement: cd has exact errors, cd - extra is too-many-args, and failed
    # chdir must not corrupt the previous-directory state.
    a_dir = tmp_workdir / "a"
    a_dir.mkdir()

    result = run(
        [
            "cd -",
            "cd a",
            "cd missing",
            "cd - extra",
            "pwd",
            "cd -",
            "pwd",
            "quit",
        ],
        cwd=tmp_workdir,
    )

    result.assert_exit_ok()
    assert result.stderr_lines() == [
        "smash error: cd: OLDPWD not set",
        "smash error: chdir failed: No such file or directory",
        "smash error: cd: too many arguments",
    ]
    assert result.stdout_lines() == [str(a_dir), str(tmp_workdir)]


def test_builtins_ignore_background_marker_and_do_not_create_jobs(smash_session, tmp_workdir: Path):
    # Requirement: built-ins execute in smash, ignore &, and must not be inserted
    # into the jobs list.
    session = smash_session(cwd=tmp_workdir)
    session.read_until_prompt()
    session.send_line("showpid &")
    session.read_until_prompt()
    session.send_line("pwd&")
    session.read_until_prompt()
    session.send_line("jobs")
    session.read_until_prompt()
    session.send_line("quit")
    result = session.finish()

    result.assert_exit_ok()
    assert parse_pid_from_showpid(result.stdout) == session.pid
    assert str(tmp_workdir) in result.stdout_lines()
    assert parse_jobs(result.stdout) == []
    assert result.stderr == ""


def test_ampersand_is_background_only_at_command_end(run):
    # Requirement: external commands run in the background only when & is the
    # syntactic command suffix after trimming surrounding whitespace.
    result = run(["/bin/echo token&not_bg", "sleep 30&   ", "jobs", "quit kill"], timeout=5)

    result.assert_exit_ok()
    assert "token&not_bg" in result.stdout_lines()
    assert parse_jobs(result.stdout) == [(1, "sleep 30&")]
    assert result.stderr == ""


def test_finished_job_cleanup_happens_before_adding_new_jobs(run):
    # Requirement: finished jobs are deleted before every command and before
    # adding a new job, so an empty list starts again from job id 1.
    result = run(["sleep 0 &", "/bin/sleep 0.15", "sleep 30 &", "jobs", "quit kill"], timeout=5)

    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &")]
    assert result.stderr == ""


def test_fg_invalid_arguments_do_not_remove_or_foreground_existing_job(run):
    # Requirement: fg syntax errors have precedence and leave the jobs list
    # unchanged.
    result = run(["sleep 30 &", "fg 1 extra", "jobs", "quit kill"], timeout=5)

    result.assert_exit_ok()
    assert result.stderr_lines() == ["smash error: fg: invalid arguments"]
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &")]


def test_kill_failed_signal_reports_perror_and_keeps_job_listed(run):
    # Requirement: kill syscall failures use perror and do not remove the job.
    result = run(["sleep 30 &", "kill -999 1", "jobs", "quit kill"], timeout=5)

    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: kill failed")
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &")]


def test_sigint_after_chprompt_uses_fixed_smash_prefix_and_shell_continues(smash_session):
    # Requirement: Ctrl-C prints the fixed smash prefix, kills only the
    # foreground process, and leaves smash alive for later commands.
    session = smash_session()
    session.read_until_prompt()
    session.send_line("chprompt sig")
    session.read_until_prompt(prompt="sig")
    session.send_line("sleep 1000")

    assert wait_for_condition(lambda: len(child_pids(session.pid)) == 1, timeout=2)
    child = child_pids(session.pid)[0]

    session.send_signal(signal.SIGINT)
    session.read_until_prompt(prompt="sig", timeout=3)
    session.send_line("showpid")
    session.read_until_prompt(prompt="sig")
    session.send_line("quit")
    result = session.finish()

    result.assert_exit_ok()
    assert "smash: got ctrl-C" in result.stdout
    assert f"smash: process {child} was killed" in result.stdout
    assert "sig: got ctrl-C" not in result.stdout
    assert f"smash pid is {session.pid}" in result.stdout
    assert result.stderr == ""


def test_redirection_exact_prompts_append_truncate_and_stdout_restore(run, tmp_workdir: Path):
    # Requirement: redirection affects only command stdout, supports truncate and
    # append, ignores &, and restores stdout for following commands/prompts.
    out = tmp_workdir / "out.txt"
    pwd_file = tmp_workdir / "pwd.txt"
    out.write_text("old\n")

    result = run(
        [
            "chprompt p",
            "/bin/echo first > out.txt",
            "/bin/echo second >> out.txt",
            "pwd > pwd.txt &",
            "/bin/echo after",
            "jobs",
            "quit",
        ],
        cwd=tmp_workdir,
    )

    result.assert_exit_ok()
    assert out.read_text() == "first\nsecond\n"
    assert pwd_file.read_text() == f"{tmp_workdir}\n"
    assert parse_jobs(result.stdout) == []
    assert re.fullmatch(r"smash> p> p> p> p> after\np> p> ", result.stdout)
    assert result.stderr == ""


def test_pipe_script_regressions_for_lf_crlf_and_missing_shebang(run, tmp_workdir: Path):
    # Requirement/regression: LF shebang scripts execute in pipe chains; direct
    # executable scripts with no shebang or CRLF shebang expose Linux exec errors.
    create_executable_script(
        tmp_workdir / "s",
        "#!/usr/bin/env bash\nawk '{ print $1 * 2 }'\n",
    )

    no_shebang = tmp_workdir / "noshebang"
    no_shebang.write_text("awk '{ print $1 * 2 }'\n")
    no_shebang.chmod(no_shebang.stat().st_mode | 0o100)

    crlf = tmp_workdir / "crlf"
    crlf.write_bytes(b"#!/bin/bash\r\nprintf 'bad\\n'\n")
    crlf.chmod(crlf.stat().st_mode | 0o100)

    result = run(
        [
            "echo 1 | ./s | ./s | ./s",
            "echo 1 | ./noshebang",
            "./crlf",
            "quit",
        ],
        cwd=tmp_workdir,
        timeout=5,
    )

    result.assert_exit_ok()
    assert result.stdout_lines() == ["8"]
    assert result.stderr_lines() == [
        "smash error: execvp failed: Exec format error",
        "smash error: execvp failed: No such file or directory",
    ]


def test_alias_strict_format_pipe_expansion_and_listing_order(run, tmp_workdir: Path):
    # Requirement: alias format follows the spec regex, aliases may contain
    # special commands, and listing preserves creation order.
    create_executable_script(
        tmp_workdir / "s",
        "#!/usr/bin/env bash\nawk '{ print $1 * 2 }'\n",
    )

    result = run(
        [
            "alias ='pwd'",
            "alias double=\"pwd\"",
            "alias bad='pwd' trailing",
            "alias pipe='echo 1|./s'",
            "alias later='/bin/echo later'",
            "pipe|./s",
            "alias",
            "quit",
        ],
        cwd=tmp_workdir,
    )

    result.assert_exit_ok()
    assert result.stderr_lines() == [
        "smash error: alias: invalid alias format",
        "smash error: alias: invalid alias format",
        "smash error: alias: invalid alias format",
    ]
    assert result.stdout_lines() == [
        "4",
        "pipe='echo 1|./s'",
        "later='/bin/echo later'",
    ]


def test_unalias_readd_places_alias_at_new_creation_position(run):
    # Requirement: alias listing order is creation order after deletions, not
    # lexical order or stale insertion position.
    result = run(
        [
            "alias a='/bin/echo a1'",
            "alias b='/bin/echo b'",
            "unalias a",
            "alias a='/bin/echo a2'",
            "alias",
            "quit",
        ]
    )

    result.assert_exit_ok()
    assert result.stdout_lines() == ["b='/bin/echo b'", "a='/bin/echo a2'"]
    assert result.stderr == ""


def test_missing_external_command_is_stderr_only_and_shell_keeps_running(run):
    # Requirement: external exec failures use perror on stderr, keep stdout clean
    # except prompts and later command output, and smash accepts more commands.
    result = run(["definitely_missing_smash_command_763451", "/bin/echo still_running", "quit"])

    result.assert_exit_ok()
    assert result.stdout_lines() == ["still_running"]
    assert result.stderr == "smash error: execvp failed: No such file or directory\n"
    _assert_no_crlf(result)
