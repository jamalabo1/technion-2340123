from __future__ import annotations

import re
from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, parse_jobs


def test_alias_lists_in_creation_order_and_is_case_sensitive(run):
    result = run(
        [
            "alias ll='/bin/echo lower'",
            "alias LL='/bin/echo upper'",
            "alias",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["ll='/bin/echo lower'", "LL='/bin/echo upper'"]
    assert_stderr_empty(result)


def test_alias_expands_builtin_external_redirection_pipe_and_background(run, tmp_workdir: Path):
    source = tmp_workdir / "source.txt"
    source.write_text("needle\nhay\n")

    result = run(
        [
            "alias p='pwd'",
            "alias e='/bin/echo alias_external'",
            "alias c='cat source.txt'",
            "alias s='sleep 30'",
            "p > pwd.txt",
            "cat pwd.txt",
            "e",
            "c | grep needle",
            "s &",
            "jobs",
            "quit kill",
        ],
        cwd=tmp_workdir,
        timeout=8,
    )
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert str(tmp_workdir) in lines
    assert "alias_external" in lines
    assert "needle" in lines
    assert parse_jobs(result.stdout) == [(1, "s &")]
    assert_stderr_empty(result)


def test_alias_expands_before_special_chars_without_affecting_later_words(run, tmp_workdir: Path):
    out = tmp_workdir / "out.txt"
    result = run(
        [
            "alias e='/bin/echo ok'",
            "e>out.txt",
            "cat out.txt",
            "/bin/echo e",
            "quit",
        ],
        cwd=tmp_workdir,
    )
    result.assert_exit_ok()
    assert out.read_text().strip() == "ok"
    assert result.stdout_lines() == ["ok", "e"]
    assert_stderr_empty(result)


def test_alias_invalid_format_cases_are_stderr(run):
    commands = [
        "alias bad-name='pwd'",
        "alias spaced = 'pwd'",
        "alias missing",
        "alias no_close='pwd",
        "alias two='pwd' extra",
        "quit",
    ]
    result = run(commands)
    result.assert_exit_ok()
    assert result.stderr.count("smash error: alias: invalid alias format") == 5
    assert "invalid alias format" not in result.stdout


def test_alias_conflict_with_existing_alias_and_reserved_command(run):
    result = run(
        [
            "alias mine='pwd'",
            "alias mine='showpid'",
            "alias pwd='showpid'",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: alias: mine already exists or is a reserved command")
    assert_stderr_contains(result, "smash error: alias: pwd already exists or is a reserved command")


def test_unalias_requires_args_and_removes_multiple_aliases(run):
    result = run(
        [
            "alias a='pwd'",
            "alias b='showpid'",
            "unalias",
            "unalias a b",
            "alias",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: unalias: not enough arguments")
    assert result.stdout_lines() == []


def test_unalias_stops_at_first_missing_name_after_partial_deletion(run):
    result = run(
        [
            "alias a='pwd'",
            "alias b='showpid'",
            "alias c='/bin/echo c'",
            "unalias a missing b",
            "alias",
            "quit",
        ]
    )
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: unalias: missing alias does not exist")
    assert result.stdout_lines() == ["b='showpid'", "c='/bin/echo c'"]


def test_alias_empty_command_is_accepted_if_spec_regex_allows_it(run):
    result = run(["alias empty=''", "alias", "quit"])
    result.assert_exit_ok()
    if result.stderr:
        assert result.stderr == "smash error: alias: invalid alias format\n"
    else:
        assert result.stdout_lines() == ["empty=''"]
