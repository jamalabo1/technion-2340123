from __future__ import annotations

from pathlib import Path

from .smash_harness import assert_stderr_empty, create_executable_script, parse_jobs


def test_alias_redirection_then_external_cat(run, tmp_workdir: Path):
    result = run(["alias p='pwd'", "p > out.txt", "cat out.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(tmp_workdir)]
    assert_stderr_empty(result)


def test_alias_pipe_over_temp_file(run, tmp_workdir: Path):
    (tmp_workdir / "file.txt").write_text("hay\nneedle\n")
    result = run(["alias c='cat file.txt'", "c | grep needle", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == ["needle"]
    assert_stderr_empty(result)


def test_cd_pwd_redirection_and_external_cat(run, tmp_workdir: Path):
    target = tmp_workdir / "target"
    target.mkdir()
    result = run(["cd target", "pwd > pwd.txt", "cat pwd.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(target)]
    assert (target / "pwd.txt").read_text().strip() == str(target)
    assert_stderr_empty(result)


def test_background_lifecycle_with_kill_fg_and_finished_cleanup(run):
    result = run(
        [
            "sleep 30 &",
            "sleep 0.1 &",
            "sleep 30 &",
            "kill -9 1",
            "fg 2",
            "/bin/sleep 0.15",
            "jobs",
            "quit kill",
        ],
        timeout=6,
    )
    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == [(3, "sleep 30 &")]
    assert_stderr_empty(result)


def test_unsetenv_external_interaction(run):
    result = run(
        ["unsetenv SMASH_COMPLEX_ENV", "/usr/bin/printenv SMASH_COMPLEX_ENV", "quit"],
        env={"SMASH_COMPLEX_ENV": "present"},
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == []
    assert_stderr_empty(result)


def test_pipe_stderr_complex_scenario(run, tmp_workdir: Path):
    script = create_executable_script(
        tmp_workdir / "writer.sh",
        "#!/bin/sh\necho normal\necho special_error >&2\n",
    )
    result = run([f"{script} |& grep special_error", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == ["normal", "special_error"]
    assert_stderr_empty(result)


def test_redirection_does_not_contaminate_later_stdout(run, tmp_workdir: Path):
    result = run(["showpid > pid.txt", "pwd", "cat pid.txt", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert lines[0] == str(tmp_workdir)
    assert lines[1].startswith("smash pid is ")
    assert_stderr_empty(result)


def test_finished_job_cleanup_before_adding_new_job_resets_empty_list_to_one(run):
    result = run(["sleep 0 &", "/bin/sleep 0.15", "jobs", "sleep 30 &", "jobs", "quit kill"], timeout=5)
    result.assert_exit_ok()
    assert parse_jobs(result.stdout) == [(1, "sleep 30 &")]
    assert_stderr_empty(result)
