from __future__ import annotations

import signal

from .smash_harness import child_pids, pid_exists, wait_for_condition


def test_sigint_when_idle_prints_ctrl_c_and_shell_continues(smash_session):
    session = smash_session()
    session.read_until_prompt()
    session.send_signal(signal.SIGINT)
    session.read_until_prompt()
    session.send_line("pwd")
    session.read_until_prompt()
    session.send_line("quit")
    result = session.finish()

    result.assert_exit_ok()
    assert "smash: got ctrl-C" in result.stdout
    assert "was killed" not in result.stdout
    assert result.stderr == ""


def test_sigint_kills_foreground_process_and_shell_continues(smash_session):
    session = smash_session()
    session.read_until_prompt()
    session.send_line("sleep 1000")

    assert wait_for_condition(lambda: len(child_pids(session.pid)) == 1, timeout=2)
    child = child_pids(session.pid)[0]

    session.send_signal(signal.SIGINT)
    session.read_until_prompt(timeout=3)
    session.send_line("showpid")
    session.read_until_prompt()
    session.send_line("quit")
    result = session.finish()

    result.assert_exit_ok()
    assert "smash: got ctrl-C" in result.stdout
    assert f"smash: process {child} was killed" in result.stdout
    assert not pid_exists(child)
    assert f"smash pid is {session.pid}" in result.stdout
    assert result.stderr == ""
