from __future__ import annotations

import re

from .smash_harness import assert_stderr_contains, assert_stderr_empty, parse_jobs


def test_jobs_empty_and_ignores_extra_args(run):
    result = run(["jobs ignored args", "quit"])
    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == []
    assert_stderr_empty(result)


def test_jobs_lists_unfinished_background_jobs_sorted_by_id(run):
    result = run(["sleep 30 &", "sleep 30 &", "sleep 30 &", "jobs", "quit kill"], timeout=6)
    result.assert_exit_ok()
    jobs = parse_jobs(result.stdout)
    assert [job_id for job_id, _ in jobs] == [1, 2, 3]
    assert [command for _, command in jobs] == ["sleep 30 &", "sleep 30 &", "sleep 30 &"]
    assert_stderr_empty(result)


def test_finished_jobs_are_removed_before_jobs_prints(run):
    result = run(["sleep 0 &", "/bin/sleep 0.15", "jobs", "quit"], timeout=3)
    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == []
    assert_stderr_empty(result)


def test_job_id_is_max_existing_plus_one_not_gap_filling(run):
    result = run(
        [
            "sleep 30 &",
            "sleep 0.1 &",
            "sleep 30 &",
            "fg 2",
            "sleep 30 &",
            "jobs",
            "quit kill",
        ],
        timeout=6,
    )
    result.assert_exit_ok()
    assert [job_id for job_id, _ in parse_jobs(result.stdout)] == [1, 3, 4]
    assert_stderr_empty(result)


def test_fg_no_args_brings_max_job_to_foreground_and_removes_it(run):
    result = run(["sleep 30 &", "sleep 0.1 &", "fg", "jobs", "quit kill"], timeout=5)
    result.assert_exit_ok()
    assert re.search(r"sleep 0\.1 & \d+", result.stdout)
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &")]
    assert_stderr_empty(result)


def test_fg_with_valid_job_id_waits_and_removes_job(run):
    result = run(["sleep 0.1 &", "sleep 30 &", "fg 1", "jobs", "quit kill"], timeout=5)
    result.assert_exit_ok()
    assert re.search(r"sleep 0\.1 & \d+", result.stdout)
    assert parse_jobs(result.stdout) == [(2, "sleep 30 &")]
    assert_stderr_empty(result)


def test_fg_errors_are_stderr_with_required_precedence(run):
    result = run(
        [
            "fg",
            "fg 1 2",
            "fg abc",
            "fg 0",
            "fg -1",
            "fg 7",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert result.stderr_lines() == [
        "smash error: fg: jobs list is empty",
        "smash error: fg: invalid arguments",
        "smash error: fg: invalid arguments",
        "smash error: fg: invalid arguments",
        "smash error: fg: invalid arguments",
        "smash error: fg: job-id 7 does not exist",
    ]
    assert "smash error: fg" not in result.stdout


def test_kill_sends_signal_and_killed_job_is_cleaned_before_later_jobs(run):
    result = run(["sleep 30 &", "kill -9 1", "/bin/sleep 0.15", "jobs", "quit"], timeout=4)
    result.assert_exit_ok()
    assert re.search(r"signal number 9 was sent to pid \d+", result.stdout)
    assert parse_jobs(result.stdout) == []
    assert_stderr_empty(result)


def test_kill_errors_are_stderr(run):
    result = run(
        [
            "kill",
            "kill -9",
            "kill 9 1",
            "kill -abc 1",
            "kill -9 abc",
            "kill -9 0",
            "kill -9 99",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert result.stderr.count("smash error: kill: invalid arguments") == 6
    assert_stderr_contains(result, "smash error: kill: job-id 99 does not exist")
    assert "smash error: kill" not in result.stdout


def test_kill_invalid_signal_that_reaches_syscall_uses_perror(run):
    result = run(["sleep 30 &", "kill -999 1", "quit kill"], timeout=4)
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: kill failed")


def test_kill_sigstop_and_sigcont_keep_job_listed(run):
    result = run(
        [
            "sleep 30 &",
            "kill -19 1",
            "jobs",
            "kill -18 1",
            "jobs",
            "quit kill",
        ],
        timeout=5,
    )
    result.assert_exit_ok()
    assert result.stdout.count("signal number 19 was sent to pid") == 1
    assert result.stdout.count("signal number 18 was sent to pid") == 1
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &"), (1, "sleep 30 &")]
    assert_stderr_empty(result)
