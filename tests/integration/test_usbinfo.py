from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


import os
import shutil
import subprocess


import pytest

from .smash_harness import assert_stderr_empty


USB_LINE_RE = re.compile(
    r"^Device (?P<devnum>\d+): ID "
    r"(?P<vendor>[0-9a-fA-F]{4}):(?P<product>[0-9a-fA-F]{4}) "
    r"(?P<description>.+) MaxPower: (?P<power>.+)mA$"
)


@dataclass(frozen=True)
class UsbDevice:
    devnum: int
    vendor: str
    product: str
    manufacturer: str
    product_name: str
    max_power: str


def _read_field(device: Path, name: str) -> str:
    field = device / name
    if not field.exists():
        return "N/A"
    value = field.read_text(errors="replace").strip()
    return value or "N/A"


def _expected_usb_devices() -> list[UsbDevice]:
    root = Path("/sys/bus/usb/devices")
    if not root.exists():
        return []

    devices: list[UsbDevice] = []
    for device in root.iterdir():
        # Root hub entries such as usb1/usb2 are present even on WSL hosts with
        # no attached USB devices. Real device entries use bus-port names like 1-2.
        if "-" not in device.name:
            continue
        if not (device / "idVendor").exists() or not (device / "idProduct").exists():
            continue
        power = _read_field(device, "bMaxPower")
        if power.endswith("mA"):
            power = power[:-2]
        devices.append(
            UsbDevice(
                devnum=int(_read_field(device, "devnum")),
                vendor=_read_field(device, "idVendor"),
                product=_read_field(device, "idProduct"),
                manufacturer=_read_field(device, "manufacturer"),
                product_name=_read_field(device, "product"),
                max_power=power,
            )
        )
    return sorted(devices, key=lambda item: item.devnum)


def test_usbinfo_no_devices_prints_exact_error_on_stderr(run):
    if _expected_usb_devices():
        pytest.skip("host exposes USB devices; no-device path is not applicable")

    result = run(["usbinfo", "quit"])
    result.assert_exit_ok()
    assert result.stdout_lines() == []
    assert result.stderr_lines() == ["smash error: usbinfo: no USB devices found"]


def test_usbinfo_lists_devices_sorted_by_device_number(run):
    expected = _expected_usb_devices()
    if not expected:
        pytest.skip("host exposes no USB devices")

    result = run(["usbinfo", "quit"])
    result.assert_exit_ok()
    assert_stderr_empty(result)

    lines = result.stdout_lines()
    assert len(lines) == len(expected)

    actual_devnums: list[int] = []
    for line, device in zip(lines, expected):
        match = USB_LINE_RE.fullmatch(line)
        assert match, f"bad usbinfo line format: {line!r}"

        actual_devnums.append(int(match.group("devnum")))
        assert int(match.group("devnum")) == device.devnum
        assert match.group("vendor").lower() == device.vendor.lower()
        assert match.group("product").lower() == device.product.lower()
        assert device.manufacturer in match.group("description")
        assert device.product_name in match.group("description")
        assert match.group("power") == device.max_power

    assert actual_devnums == sorted(actual_devnums)




def _write_usb_field(device: Path, name: str, value: str) -> None:
    (device / name).write_text(value + "\n")


def _make_fake_usb_device(
    root: Path,
    name: str,
    *,
    devnum: int,
    vendor: str,
    product: str,
    manufacturer: str,
    product_name: str,
    max_power: str,
) -> None:
    device = root / name
    device.mkdir(parents=True)

    _write_usb_field(device, "devnum", str(devnum))
    _write_usb_field(device, "idVendor", vendor)
    _write_usb_field(device, "idProduct", product)
    _write_usb_field(device, "manufacturer", manufacturer)
    _write_usb_field(device, "product", product_name)
    _write_usb_field(device, "bMaxPower", max_power)


def _can_use_mount_namespace() -> bool:
    if shutil.which("unshare") is None:
        return False
    if shutil.which("mount") is None:
        return False

    probe = subprocess.run(
        ["unshare", "--mount", "true"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return probe.returncode == 0


@pytest.mark.integration
def test_usbinfo_with_bind_mounted_fake_sysfs_tree(tmp_path: Path):
    if not _can_use_mount_namespace():
        pytest.skip("mount namespace requires unshare + CAP_SYS_ADMIN")

    smash_bin = os.environ.get("SMASH_BIN")
    if not smash_bin:
        pytest.skip("set SMASH_BIN=/path/to/smash to run this integration test")

    fake_root = tmp_path / "fake_usb_devices"
    fake_root.mkdir()

    # Should be ignored: root hub style entry.
    (fake_root / "usb1").mkdir()

    # Should be ignored: interface entry, not a physical device.
    (fake_root / "1-8:1.0").mkdir()

    # Should be ignored: missing idVendor/idProduct.
    (fake_root / "1-99").mkdir()

    _make_fake_usb_device(
        fake_root,
        "1-8",
        devnum=8,
        vendor="abcd",
        product="1234",
        manufacturer="Acme",
        product_name="Keyboard",
        max_power="100mA",
    )

    _make_fake_usb_device(
        fake_root,
        "1-3",
        devnum=3,
        vendor="0bad",
        product="cafe",
        manufacturer="MegaCorp",
        product_name="Flash Drive",
        max_power="500mA",
    )

    script = f"""
set -eu

mount --make-rprivate /
mount --bind {fake_root} /sys/bus/usb/devices

printf 'usbinfo\\nquit\\n' | {smash_bin}
"""

    result = subprocess.run(
        ["unshare", "--mount", "bash", "-c", script],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert result.returncode == 0
    assert result.stderr == ""

    lines = result.stdout.splitlines()

    assert lines == [
        "Device 3: ID 0bad:cafe MegaCorp Flash Drive MaxPower: 500mA",
        "Device 8: ID abcd:1234 Acme Keyboard MaxPower: 100mA",
    ]