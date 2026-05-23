from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Sequence

import pytest

from .smash_harness import SmashRunResult, SmashSession, run_smash


LINUX_ONLY = pytest.mark.skipif(sys.platform != "linux", reason="smash is a Linux shell assignment")


def pytest_collection_modifyitems(config, items):
    if sys.platform != "linux":
        for item in items:
            item.add_marker(LINUX_ONLY)


@pytest.fixture(scope="session")
def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def smash_executable(project_root: Path) -> Path:
    env_path = os.environ.get("SMASH_TEST_BINARY")
    if env_path:
        binary = Path(env_path)
        if not binary.exists():
            pytest.fail(f"SMASH_TEST_BINARY does not exist: {binary}")
        return binary

    build_dir = Path(os.environ.get("SMASH_CMAKE_BUILD_DIR", project_root / "cmake-build-debug-vm"))
    if not (build_dir / "CMakeCache.txt").exists():
        configure = subprocess.run(
            ["cmake", "-S", str(project_root), "-B", str(build_dir)],
            cwd=project_root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        if configure.returncode != 0:
            pytest.fail(
                "failed to configure CMake build for smash\n"
                f"stdout:\n{configure.stdout}\n"
                f"stderr:\n{configure.stderr}"
            )

    build = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "skeleton_smash"],
        cwd=project_root,
        text=True,
        capture_output=True,
        timeout=90,
    )
    if build.returncode != 0:
        pytest.fail(
            "failed to build smash with CMake target `skeleton_smash`\n"
            f"stdout:\n{build.stdout}\n"
            f"stderr:\n{build.stderr}"
        )

    binary = build_dir / "skeleton_smash"
    if not binary.exists() or not os.access(binary, os.X_OK):
        pytest.fail(f"CMake build completed but executable is missing or not executable: {binary}")
    return binary


@pytest.fixture(scope="session")
def smash_binary(smash_executable: Path) -> Path:
    return smash_executable


@pytest.fixture()
def tmp_workdir(tmp_path: Path) -> Path:
    return tmp_path


@pytest.fixture()
def smash_session(smash_executable: Path, tmp_workdir: Path):
    sessions: list[SmashSession] = []

    def factory(cwd: Path | str | None = None, env: dict[str, str] | None = None) -> SmashSession:
        session = SmashSession(smash_executable, cwd=cwd or tmp_workdir, env=env)
        sessions.append(session)
        return session

    yield factory

    for session in sessions:
        session.terminate()


@pytest.fixture()
def run(smash_executable: Path, tmp_workdir: Path):
    def runner(
        commands: Sequence[str],
        cwd: Path | str | None = None,
        timeout: float = 5.0,
        env: dict[str, str] | None = None,
    ) -> SmashRunResult:
        return run_smash(
            binary=smash_executable,
            commands=commands,
            cwd=cwd or tmp_workdir,
            timeout=timeout,
            env=env,
        )

    return runner
