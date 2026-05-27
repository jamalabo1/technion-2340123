from __future__ import annotations

import errno
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from .smash_harness import SmashSession, open_fd_count, wait_for_condition


FAULT_INJECTOR_SOURCE = r"""
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int maybe_fail(const char* syscall_name, int default_errno) {
    const char* path = getenv("SMASH_FAIL_SYSCALL_CONTROL");
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    int fd = (int)syscall(SYS_openat, AT_FDCWD, path, O_RDWR | O_CLOEXEC, 0);
    if (fd == -1) {
        return 0;
    }

    char buffer[128];
    ssize_t size = syscall(SYS_read, fd, buffer, sizeof(buffer) - 1);
    if (size <= 0) {
        syscall(SYS_close, fd);
        return 0;
    }
    buffer[size] = '\0';

    char armed_name[32];
    int remaining = 0;
    int err = default_errno;
    if (sscanf(buffer, "%31s %d %d", armed_name, &remaining, &err) != 3 ||
        strcmp(armed_name, syscall_name) != 0 ||
        remaining <= 0) {
        syscall(SYS_close, fd);
        return 0;
    }

    --remaining;
    char updated[128];
    int updated_size = snprintf(updated, sizeof(updated), "%s %d %d\n", armed_name, remaining, err);
    syscall(SYS_lseek, fd, 0, SEEK_SET);
    syscall(SYS_write, fd, updated, updated_size);
    syscall(SYS_ftruncate, fd, updated_size);
    syscall(SYS_close, fd);

    errno = err;
    return 1;
}

int open(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    if (maybe_fail("open", EACCES)) {
        return -1;
    }

    int (*real_open)(const char*, int, ...) = dlsym(RTLD_NEXT, "open");
    if ((flags & O_CREAT) != 0) {
        return real_open(pathname, flags, mode);
    }
    return real_open(pathname, flags);
}

int open64(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    if (maybe_fail("open", EACCES)) {
        return -1;
    }

    int (*real_open64)(const char*, int, ...) = dlsym(RTLD_NEXT, "open64");
    if ((flags & O_CREAT) != 0) {
        return real_open64(pathname, flags, mode);
    }
    return real_open64(pathname, flags);
}

int pipe(int pipefd[2]) {
    if (maybe_fail("pipe", EMFILE)) {
        return -1;
    }

    int (*real_pipe)(int[2]) = dlsym(RTLD_NEXT, "pipe");
    return real_pipe(pipefd);
}

int dup(int oldfd) {
    if (maybe_fail("dup", EMFILE)) {
        return -1;
    }

    int (*real_dup)(int) = dlsym(RTLD_NEXT, "dup");
    return real_dup(oldfd);
}

int dup2(int oldfd, int newfd) {
    if (maybe_fail("dup2", EBADF)) {
        return -1;
    }

    int (*real_dup2)(int, int) = dlsym(RTLD_NEXT, "dup2");
    return real_dup2(oldfd, newfd);
}

pid_t fork(void) {
    if (maybe_fail("fork", EAGAIN)) {
        return -1;
    }

    pid_t (*real_fork)(void) = dlsym(RTLD_NEXT, "fork");
    return real_fork();
}
"""


@pytest.fixture(scope="session")
def syscall_fault_injector(tmp_path_factory: pytest.TempPathFactory) -> Path:
    gcc = shutil.which(os.environ.get("CC", "gcc"))
    if gcc is None:
        pytest.skip("gcc is required to build the syscall fault injector")

    build_dir = tmp_path_factory.mktemp("syscall_fault_injector")
    source = build_dir / "syscall_fault_injector.c"
    shared_object = build_dir / "libsyscall_fault_injector.so"
    source.write_text(FAULT_INJECTOR_SOURCE)

    result = subprocess.run(
        [gcc, "-shared", "-fPIC", "-ldl", "-o", str(shared_object), str(source)],
        text=True,
        capture_output=True,
        timeout=30,
    )
    if result.returncode != 0:
        pytest.fail(
            "failed to build syscall fault injector\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return shared_object


@pytest.fixture()
def syscall_failure_env(syscall_fault_injector: Path, tmp_workdir: Path):
    control = tmp_workdir / "syscall_fail_control.txt"
    control.write_text("")

    env = {
        "LD_PRELOAD": str(syscall_fault_injector),
        "SMASH_FAIL_SYSCALL_CONTROL": str(control),
    }

    def arm(syscall_name: str, err: int, count: int = 1) -> None:
        control.write_text(f"{syscall_name} {count} {err}\n")

    return env, arm


def start_fault_injected_session(smash_executable: Path, tmp_workdir: Path, env: dict[str, str]) -> SmashSession:
    session = SmashSession(smash_executable, cwd=tmp_workdir, env=env)
    assert wait_for_condition(lambda: session.stdout.endswith("smash> "), timeout=2.0)
    return session


def assert_prompt_returns(session: SmashSession, prompt: str, start: int) -> None:
    marker = f"{prompt}> "
    assert wait_for_condition(lambda: marker in session.stdout[start:], timeout=5.0), (
        f"shell did not recover and print prompt {marker!r}\n"
        f"stdout:\n{session.stdout}\n"
        f"stderr:\n{session.stderr}"
    )


def test_mocked_open_failure_reports_error_and_next_redirection_recovers(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("open", errno.EACCES)
    session.send_line("pwd > denied_once.txt")
    session.send_line("pwd > recovered.txt")
    session.send_line("chprompt open_recovered")

    assert_prompt_returns(session, "open_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: open failed" in result.stderr
    assert not (tmp_workdir / "denied_once.txt").exists()
    assert (tmp_workdir / "recovered.txt").read_text().strip() == str(tmp_workdir)


def test_mocked_dup_failure_closes_open_file_and_recovers_stdout(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("dup", errno.EMFILE)
    session.send_line("pwd > dup_failed.txt")
    session.send_line("/bin/echo stdout-recovered")
    session.send_line("pwd > dup_recovered.txt")
    session.send_line("chprompt dup_recovered")

    assert_prompt_returns(session, "dup_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: dup failed" in result.stderr
    assert "stdout-recovered" in result.stdout
    assert (tmp_workdir / "dup_failed.txt").exists()
    assert (tmp_workdir / "dup_failed.txt").read_text() == ""
    assert (tmp_workdir / "dup_recovered.txt").read_text().strip() == str(tmp_workdir)


def test_mocked_dup2_failure_restores_stdout_and_recovers_next_command(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("dup2", errno.EBADF)
    session.send_line("pwd > dup2_failed.txt")
    session.send_line("/bin/echo stdout-after-dup2")
    session.send_line("pwd > dup2_recovered.txt")
    session.send_line("chprompt dup2_recovered")

    assert_prompt_returns(session, "dup2_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: dup2 failed" in result.stderr
    assert "stdout-after-dup2" in result.stdout
    assert (tmp_workdir / "dup2_failed.txt").exists()
    assert (tmp_workdir / "dup2_failed.txt").read_text() == ""
    assert (tmp_workdir / "dup2_recovered.txt").read_text().strip() == str(tmp_workdir)


def test_mocked_pipe_failure_reports_error_and_next_pipe_recovers(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("pipe", errno.EMFILE)
    session.send_line("/bin/echo lost | /bin/cat")
    session.send_line("/bin/echo pipe-ok | /bin/cat")
    session.send_line("chprompt pipe_recovered")

    assert_prompt_returns(session, "pipe_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: pipe failed" in result.stderr
    assert "lost" not in result.stdout
    assert "pipe-ok" in result.stdout


def test_mocked_external_fork_failure_reports_error_and_next_external_recovers(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("fork", errno.EAGAIN)
    session.send_line("/bin/echo lost-fork")
    session.send_line("/bin/echo fork-ok")
    session.send_line("chprompt fork_recovered")

    assert_prompt_returns(session, "fork_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: fork failed" in result.stderr
    assert "lost-fork" not in result.stdout
    assert "fork-ok" in result.stdout


def test_mocked_pipe_first_fork_failure_closes_pipe_fds_and_recovers(
    smash_executable: Path,
    tmp_workdir: Path,
    syscall_failure_env,
):
    env, arm = syscall_failure_env
    session = start_fault_injected_session(smash_executable, tmp_workdir, env)
    baseline = open_fd_count(session.pid)

    start = len(session.stdout)
    arm("fork", errno.EAGAIN)
    session.send_line("/bin/echo lost-pipe-fork | /bin/cat")
    session.send_line("/bin/echo pipe-fork-ok | /bin/cat")
    session.send_line("chprompt pipe_fork_recovered")

    assert_prompt_returns(session, "pipe_fork_recovered", start)
    after = open_fd_count(session.pid)
    session.send_line("quit")
    result = session.finish(timeout=3.0)
    result.assert_exit_ok()

    assert after <= baseline + 3
    assert "smash error: fork failed" in result.stderr
    assert "lost-pipe-fork" not in result.stdout
    assert "pipe-fork-ok" in result.stdout
