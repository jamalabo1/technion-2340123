from __future__ import annotations

import datetime as dt
import os
import platform
import re
from pathlib import Path

from .smash_harness import assert_stderr_contains, assert_stderr_empty, expected_du_kb


def _proc_text(path: str) -> str:
    return Path(path).read_text().strip()


def test_sysinfo_prints_exact_fields_and_linux_values(run):
    result = run(["sysinfo ignored args", "quit"])
    result.assert_exit_ok()
    lines = result.stdout_lines()
    assert len(lines) == 5
    assert lines[0] == f"System: {_proc_text('/proc/sys/kernel/ostype')}"
    assert lines[1] == f"Hostname: {_proc_text('/proc/sys/kernel/hostname')}"
    assert lines[2] == f"Kernel: {_proc_text('/proc/sys/kernel/osrelease')}"
    assert lines[3] == f"Architecture: {platform.machine()}"

    match = re.fullmatch(r"Boot Time: (\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})", lines[4])
    assert match, f"bad boot-time line: {lines[4]!r}"
    btime = next(line for line in Path("/proc/stat").read_text().splitlines() if line.startswith("btime "))
    expected = dt.datetime.fromtimestamp(int(btime.split()[1])).strftime("%Y-%m-%d %H:%M:%S")
    assert match.group(1) == expected
    assert_stderr_empty(result)


def test_unsetenv_errors_are_stderr(run):
    result = run(["unsetenv", "unsetenv DEFINITELY_MISSING_SMASH_ENV", "quit"])
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: unsetenv: not enough arguments")
    assert_stderr_contains(result, "smash error: unsetenv: DEFINITELY_MISSING_SMASH_ENV does not exist")
    assert "unsetenv" not in result.stdout


def test_unsetenv_removes_variables_for_later_external_children(run):
    env = {
        "SMASH_TEST_ENV_A": "value_a",
        "SMASH_TEST_ENV_B": "value_b",
    }
    result = run(
        [
            "unsetenv SMASH_TEST_ENV_A",
            "/usr/bin/printenv SMASH_TEST_ENV_A",
            "/usr/bin/printenv SMASH_TEST_ENV_B",
            "quit",
        ],
        env=env,
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["value_b"]
    assert_stderr_empty(result)


def test_unsetenv_stops_at_first_missing_variable_after_partial_deletion(run):
    env = {
        "SMASH_TEST_ENV_A": "value_a",
        "SMASH_TEST_ENV_B": "value_b",
        "SMASH_TEST_ENV_C": "value_c",
    }
    result = run(
        [
            "unsetenv SMASH_TEST_ENV_A SMASH_TEST_ENV_MISSING SMASH_TEST_ENV_B",
            "/usr/bin/printenv SMASH_TEST_ENV_A",
            "/usr/bin/printenv SMASH_TEST_ENV_B",
            "/usr/bin/printenv SMASH_TEST_ENV_C",
            "quit",
        ],
        env=env,
    )
    result.assert_exit_ok()
    assert result.stdout_lines() == ["value_b", "value_c"]
    assert_stderr_contains(result, "smash error: unsetenv: SMASH_TEST_ENV_MISSING does not exist")


def _assert_du_output(result, expected_kb: int) -> None:
    result.assert_exit_ok()
    assert result.stdout_lines() == [f"Total disk usage: {expected_kb} KB"]
    assert_stderr_empty(result)


def test_du_uses_current_directory_for_no_args_and_accepts_relative_path(run, tmp_workdir: Path):
    child = tmp_workdir / "child"
    child.mkdir()
    (child / "one.txt").write_text("x")

    result = run(["cd child", "du", "du .", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    expected = f"Total disk usage: {expected_du_kb(child)} KB"
    assert result.stdout_lines() == [expected, expected]
    assert_stderr_empty(result)


def test_du_absolute_path_nested_files_and_symlinks_not_followed(run, tmp_workdir: Path):
    root = tmp_workdir / "walked"
    nested = root / "nested"
    outside = tmp_workdir / "outside"
    nested.mkdir(parents=True)
    outside.mkdir()
    (root / "root.txt").write_text("r")
    (nested / "nested.txt").write_text("n" * 4097)
    (outside / "large.txt").write_text("z" * 128 * 1024)
    os.symlink(outside / "large.txt", root / "file_link")
    os.symlink(outside, root / "dir_link")

    result = run([f"du {root}", "quit"], cwd=tmp_workdir)
    _assert_du_output(result, expected_du_kb(root))


def test_du_empty_directory_and_one_byte_file(run, tmp_workdir: Path):
    empty = tmp_workdir / "empty"
    one = tmp_workdir / "one"
    empty.mkdir()
    one.mkdir()
    (one / "byte").write_text("x")

    result = run([f"du {empty}", f"du {one}", "quit"], cwd=tmp_workdir)
    result.assert_exit_ok()
    assert result.stdout_lines() == [
        f"Total disk usage: {expected_du_kb(empty)} KB",
        f"Total disk usage: {expected_du_kb(one)} KB",
    ]
    assert_stderr_empty(result)


def test_du_too_many_arguments_is_stderr(run):
    result = run(["du a b", "quit"])
    result.assert_exit_ok()
    assert_stderr_contains(result, "smash error: du: too many arguments")
    assert "too many arguments" not in result.stdout


def test_usbinfo_black_box_output_matches_host_usb_state(run):
    usb_root = Path("/sys/bus/usb/devices")
    has_devices = usb_root.exists() and any((p / "idVendor").exists() for p in usb_root.iterdir())
    result = run(["usbinfo", "quit"])
    result.assert_exit_ok()
    if not has_devices:
        assert_stderr_contains(result, "smash error: usbinfo: no USB devices found")
        return

    assert_stderr_empty(result)
    devnums = []
    for line in result.stdout_lines():
        match = re.fullmatch(
            r"Device (\d+): ID [0-9a-fA-F]{4}:[0-9a-fA-F]{4} .+ MaxPower: \d+mA",
            line,
        )
        assert match, f"bad usbinfo line: {line!r}"
        devnums.append(int(match.group(1)))
    assert devnums == sorted(devnums)
