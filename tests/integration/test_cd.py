from __future__ import annotations

from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty


def test_cd_relative_absolute_parent_and_no_args(run, tmp_workdir: Path):
    rel = tmp_workdir / "rel"
    abs_dir = tmp_workdir / "abs"
    nested = rel / "nested"
    nested.mkdir(parents=True)
    abs_dir.mkdir()

    result = run(
        [
            "pwd",
            "cd",
            "pwd",
            "cd rel/nested",
            "pwd",
            f"cd {abs_dir}",
            "pwd",
            "cd ..",
            "pwd",
            "quit",
        ],
        cwd=tmp_workdir,
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == [
        str(tmp_workdir),
        str(tmp_workdir),
        str(nested),
        str(abs_dir),
        str(tmp_workdir),
    ]
    assert_stderr_empty(result)


def test_cd_dash_toggles_previous_directory(run, tmp_workdir: Path):
    a = tmp_workdir / "a"
    b = tmp_workdir / "b"
    a.mkdir()
    b.mkdir()

    result = run(["cd a", f"cd {b}", "cd -", "pwd", "cd -", "pwd", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [str(a), str(b)]
    assert_stderr_empty(result)


def test_cd_dash_before_previous_directory_errors_on_stderr(run, tmp_workdir: Path):
    result = run(["cd -", "pwd", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: cd: OLDPWD not set")
    assert "OLDPWD not set" not in result.stdout
    assert result.stdout_lines() == [str(tmp_workdir)]


def test_cd_too_many_arguments_precedes_chdir_failure(run, tmp_workdir: Path):
    result = run(["cd missing another", "pwd", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: cd: too many arguments")
    assert "chdir failed" not in result.stderr
    assert result.stdout_lines() == [str(tmp_workdir)]


def test_failed_chdir_preserves_cwd_and_oldpwd(run, tmp_workdir: Path):
    good = tmp_workdir / "good"
    good.mkdir()

    result = run(
        ["cd good", "cd missing", "pwd", "cd -", "pwd", "quit"],
        cwd=tmp_workdir,
    )
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: chdir failed")
    assert result.stdout_lines() == [str(good), str(tmp_workdir)]
