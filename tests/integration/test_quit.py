from __future__ import annotations

import re

from .smash_harness import assert_stderr_empty, pid_exists, wait_for_condition


def test_quit_exits_cleanly_and_ignores_non_kill_extra_args(run):
    result = run(["quit ignored extra"])
    result.assert_exit_ok()
    assert result.stdout.startswith("smash> ")
    assert_stderr_empty(result)


def test_quit_kill_no_jobs_prints_zero_jobs_header(run):
    result = run(["quit kill"])
    result.assert_exit_ok()
    assert result.stdout_lines() == ["smash: sending SIGKILL signal to 0 jobs:"]
    assert_stderr_empty(result)


def test_quit_kill_lists_and_kills_all_unfinished_jobs(run):
    result = run(["sleep 30 &", "sleep 30 &", "quit kill ignored"], timeout=5)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert "smash: sending SIGKILL signal to 2 jobs:" in lines
    pid_lines = [line for line in lines if re.match(r"^\d+: sleep 30 &$", line)]
    assert len(pid_lines) == 2
    pids = [int(line.split(":", 1)[0]) for line in pid_lines]
    assert wait_for_condition(lambda: all(not pid_exists(pid) for pid in pids), timeout=2)
    assert_stderr_empty(result)


def test_quit_kill_after_fg_and_kill_reports_only_remaining_jobs(run):
    result = run(
        [
            "sleep 0.1 &",
            "sleep 30 &",
            "sleep 30 &",
            "fg 1",
            "kill -9 2",
            "/bin/sleep 0.15",
            "quit kill",
        ],
        timeout=6,
    )
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert "smash: sending SIGKILL signal to 1 jobs:" in lines
    pid_lines = [line for line in lines if re.match(r"^\d+: sleep 30 &$", line)]
    assert len(pid_lines) == 1
    assert_stderr_empty(result)
