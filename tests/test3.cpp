#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Assignment syscall numbers (x86-64 modified kernel).
static constexpr long SYS_SET_BAN          = 334;
static constexpr long SYS_GET_BAN          = 335;
static constexpr long SYS_CHECK_BAN        = 336;
static constexpr long SYS_FLIP_BAN_BRANCH  = 337;

static long set_ban(int g, int p, int k, int s) {
    errno = 0;
    return syscall(SYS_SET_BAN, g, p, k, s);
}

static long get_ban(char ban) {
    errno = 0;
    return syscall(SYS_GET_BAN, ban);
}

static long check_ban(pid_t pid, char ban) {
    errno = 0;
    return syscall(SYS_CHECK_BAN, pid, ban);
}

static long flip_ban_branch(int height, char ban) {
    errno = 0;
    return syscall(SYS_FLIP_BAN_BRANCH, height, ban);
}

static int fail(const char* what) {
    dprintf(STDERR_FILENO, "    %s (errno=%d: %s)\n", what, errno, strerror(errno));
    return 1;
}

static bool expect_ok(long rc, const char* what) {
    if (rc != 0) {
        dprintf(STDERR_FILENO, "    %s: expected 0, got %ld (errno=%d: %s)\n",
                what, rc, errno, strerror(errno));
        return false;
    }
    return true;
}

static bool expect_value(long rc, long expected, const char* what) {
    if (rc != expected) {
        dprintf(STDERR_FILENO, "    %s: expected %ld, got %ld (errno=%d: %s)\n",
                what, expected, rc, errno, strerror(errno));
        return false;
    }
    return true;
}

static bool expect_errno(long rc, int expected_errno, const char* what) {
    if (rc != -1 || errno != expected_errno) {
        dprintf(STDERR_FILENO,
                "    %s: expected rc=-1, errno=%d; got rc=%ld, errno=%d (%s)\n",
                what, expected_errno, rc, errno, strerror(errno));
        return false;
    }
    return true;
}

enum TestExit {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 77,
};

static int require_root() {
    if (geteuid() != 0) {
        dprintf(STDERR_FILENO, "    requires root; run with sudo\n");
        return TEST_SKIP;
    }
    return TEST_PASS;
}

static int test_set_ban_errors() {
    // Drop privileges when possible. This tests both error priority and EPERM.
    if (geteuid() == 0 && setuid(65534) != 0)
        return fail("setuid(65534) failed");

    if (!expect_errno(set_ban(-1, 0, 0, 0), EINVAL,
                      "set_ban invalid argument has priority over EPERM"))
        return TEST_FAIL;

    if (!expect_errno(set_ban(0, 0, 0, 0), EPERM,
                      "set_ban valid call by non-root"))
        return TEST_FAIL;

    return TEST_PASS;
}

static int test_set_and_get_bans() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    // Values greater than 1 must be treated as 1.
    if (!expect_ok(set_ban(2, 0, 99, 0), "set_ban(2,0,99,0)"))
        return TEST_FAIL;

    if (!expect_value(get_ban('g'), 1, "get_ban('g')")) return TEST_FAIL;
    if (!expect_value(get_ban('p'), 0, "get_ban('p')")) return TEST_FAIL;
    if (!expect_value(get_ban('k'), 1, "get_ban('k')")) return TEST_FAIL;
    if (!expect_value(get_ban('s'), 0, "get_ban('s')")) return TEST_FAIL;

    if (!expect_errno(get_ban('x'), EINVAL, "get_ban invalid character"))
        return TEST_FAIL;

    return TEST_PASS;
}

static int test_unbanned_syscalls_work() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    if (!expect_ok(set_ban(0, 0, 0, 0), "clear all bans"))
        return TEST_FAIL;

    const pid_t me = static_cast<pid_t>(syscall(SYS_getpid));
    if (me <= 0) return fail("unbanned getpid failed");

    int fds[2] = {-1, -1};
    errno = 0;
    if (syscall(SYS_pipe, fds) != 0) return fail("unbanned pipe failed");
    close(fds[0]);
    close(fds[1]);

    errno = 0;
    if (syscall(SYS_kill, me, 0) != 0) return fail("unbanned kill(pid, 0) failed");

    errno = 0;
    if (signal(SIGUSR1, SIG_IGN) == SIG_ERR) return fail("unbanned signal failed");

    return TEST_PASS;
}

static int test_ban_getpid() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(1, 0, 0, 0), "ban getpid")) return TEST_FAIL;

    errno = 0;
    return expect_errno(syscall(SYS_getpid), EPERM, "blocked getpid")
               ? TEST_PASS : TEST_FAIL;
}

static int test_ban_pipe() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(0, 1, 0, 0), "ban pipe")) return TEST_FAIL;

    int fds[2] = {-1, -1};
    errno = 0;
    return expect_errno(syscall(SYS_pipe, fds), EPERM, "blocked pipe")
               ? TEST_PASS : TEST_FAIL;
}

static int test_ban_kill() {
    if (require_root() != TEST_PASS) return TEST_SKIP;

    const pid_t me = static_cast<pid_t>(syscall(SYS_getpid));
    if (me <= 0) return fail("getpid before banning kill failed");

    if (!expect_ok(set_ban(0, 0, 1, 0), "ban kill")) return TEST_FAIL;

    errno = 0;
    return expect_errno(syscall(SYS_kill, me, 0), EPERM, "blocked kill")
               ? TEST_PASS : TEST_FAIL;
}

static int test_ban_signal() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(0, 0, 0, 1), "ban signal")) return TEST_FAIL;

    errno = 0;
    return expect_errno(signal(SIGUSR1, SIG_IGN) == SIG_ERR ? -1 : 0,
                        EPERM, "blocked signal/rt_sigaction")
               ? TEST_PASS : TEST_FAIL;
}

static int test_fork_inheritance() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(1, 0, 1, 0), "set parent bans")) return TEST_FAIL;

    pid_t child = fork();
    if (child < 0) return fail("fork failed");

    if (child == 0) {
        bool ok = true;
        ok &= expect_value(get_ban('g'), 1, "child inherited g");
        ok &= expect_value(get_ban('p'), 0, "child inherited p=0");
        ok &= expect_value(get_ban('k'), 1, "child inherited k");
        ok &= expect_value(get_ban('s'), 0, "child inherited s=0");
        _exit(ok ? TEST_PASS : TEST_FAIL);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) return fail("waitpid failed");
    return WIFEXITED(status) && WEXITSTATUS(status) == TEST_PASS
               ? TEST_PASS : TEST_FAIL;
}

static int test_check_ban() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(0, 0, 0, 0), "clear caller bans")) return TEST_FAIL;

    int ready[2];
    if (pipe(ready) != 0) return fail("pipe for synchronization failed");

    pid_t target = fork();
    if (target < 0) return fail("fork target failed");

    if (target == 0) {
        close(ready[0]);
        if (set_ban(1, 0, 1, 0) != 0) _exit(TEST_FAIL);
        const char byte = 'R';
        if (write(ready[1], &byte, 1) != 1) _exit(TEST_FAIL);
        for (;;) pause();
    }

    close(ready[1]);
    char byte = 0;
    if (read(ready[0], &byte, 1) != 1) {
        kill(target, SIGKILL);
        waitpid(target, nullptr, 0);
        return fail("failed waiting for target readiness");
    }
    close(ready[0]);

    bool ok = true;
    ok &= expect_value(check_ban(target, 'g'), 1, "target has getpid ban");
    ok &= expect_value(check_ban(target, 'p'), 0, "target has no pipe ban");
    ok &= expect_value(check_ban(target, 'k'), 1, "target has kill ban");
    ok &= expect_errno(check_ban(target, 'x'), EINVAL, "invalid ban character");

    // The caller is now banned from checking 'g'. The target still exists.
    ok &= expect_ok(set_ban(1, 0, 0, 0), "ban caller from getpid");
    ok &= expect_errno(check_ban(target, 'g'), EPERM,
                       "caller ban prevents check_ban");

    // Cleanup is still possible because kill itself is not banned for the caller.
    if (kill(target, SIGKILL) != 0) ok = false;
    if (waitpid(target, nullptr, 0) < 0) ok = false;

    // Error priority: ESRCH must be returned before checking caller's ban.
    ok &= expect_errno(check_ban(target, 'g'), ESRCH,
                       "ESRCH has priority over caller EPERM");

    return ok ? TEST_PASS : TEST_FAIL;
}

struct FlipReport {
    long flip_rc;
    int flip_errno;
    long parent_ban;
    int parent_errno;
    long grandparent_ban;
    int grandparent_errno;
};

static int test_flip_ban_branch() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(0, 0, 0, 0), "clear grandparent bans")) return TEST_FAIL;

    const pid_t grandparent_pid = static_cast<pid_t>(syscall(SYS_getpid));
    int report_pipe[2];
    if (pipe(report_pipe) != 0) return fail("report pipe failed");

    pid_t parent = fork();
    if (parent < 0) return fail("fork parent failed");

    if (parent == 0) {
        close(report_pipe[0]);
        if (set_ban(0, 0, 0, 0) != 0) _exit(TEST_FAIL);

        const pid_t parent_pid = static_cast<pid_t>(syscall(SYS_getpid));
        pid_t caller = fork();
        if (caller < 0) _exit(TEST_FAIL);

        if (caller == 0) {
            FlipReport report{};

            errno = 0;
            report.flip_rc = flip_ban_branch(2, 'g');
            report.flip_errno = errno;

            errno = 0;
            report.parent_ban = check_ban(parent_pid, 'g');
            report.parent_errno = errno;

            errno = 0;
            report.grandparent_ban = check_ban(grandparent_pid, 'g');
            report.grandparent_errno = errno;

            const ssize_t n = write(report_pipe[1], &report, sizeof(report));
            _exit(n == static_cast<ssize_t>(sizeof(report)) ? TEST_PASS : TEST_FAIL);
        }

        int status = 0;
        waitpid(caller, &status, 0);
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : TEST_FAIL);
    }

    close(report_pipe[1]);
    FlipReport report{};
    const ssize_t n = read(report_pipe[0], &report, sizeof(report));
    close(report_pipe[0]);

    int parent_status = 0;
    if (waitpid(parent, &parent_status, 0) < 0) return fail("waitpid parent failed");

    bool ok = true;
    if (n != static_cast<ssize_t>(sizeof(report))) {
        dprintf(STDERR_FILENO, "    did not receive full flip report\n");
        ok = false;
    }
    if (report.flip_rc != 2) {
        dprintf(STDERR_FILENO, "    flip expected 2 newly banned parents, got %ld (errno=%d)\n",
                report.flip_rc, report.flip_errno);
        ok = false;
    }
    if (report.parent_ban != 1) {
        dprintf(STDERR_FILENO, "    direct parent was not banned (rc=%ld, errno=%d)\n",
                report.parent_ban, report.parent_errno);
        ok = false;
    }
    if (report.grandparent_ban != 1) {
        dprintf(STDERR_FILENO, "    grandparent was not banned (rc=%ld, errno=%d)\n",
                report.grandparent_ban, report.grandparent_errno);
        ok = false;
    }
    if (!expect_value(get_ban('g'), 1, "grandparent's own ban after flip"))
        ok = false;

    if (!WIFEXITED(parent_status) || WEXITSTATUS(parent_status) != TEST_PASS)
        ok = false;

    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_flip_errors() {
    if (require_root() != TEST_PASS) return TEST_SKIP;
    if (!expect_ok(set_ban(0, 0, 0, 0), "clear bans")) return TEST_FAIL;

    bool ok = true;
    ok &= expect_errno(flip_ban_branch(0, 'g'), EINVAL, "height=0");
    ok &= expect_errno(flip_ban_branch(-3, 'g'), EINVAL, "negative height");
    ok &= expect_errno(flip_ban_branch(1, 'x'), EINVAL, "invalid ban char");

    ok &= expect_ok(set_ban(1, 0, 0, 0), "ban caller from getpid");
    ok &= expect_errno(flip_ban_branch(1, 'g'), EPERM,
                       "caller ban prevents flip");

    return ok ? TEST_PASS : TEST_FAIL;
}

struct TestCase {
    const char* name;
    int (*fn)();
};

static int run_isolated(const TestCase& test) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return TEST_FAIL;
    }

    if (pid == 0) {
        const int rc = test.fn();
        _exit(rc);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return TEST_FAIL;
    }

    if (!WIFEXITED(status)) {
        std::printf("[FAIL] %s (terminated by signal %d)\n",
                    test.name, WTERMSIG(status));
        return TEST_FAIL;
    }

    const int rc = WEXITSTATUS(status);
    if (rc == TEST_PASS)
        std::printf("[PASS] %s\n", test.name);
    else if (rc == TEST_SKIP)
        std::printf("[SKIP] %s\n", test.name);
    else
        std::printf("[FAIL] %s\n", test.name);

    return rc;
}

int main() {
    const TestCase tests[] = {
        {"set_ban errors and priority", test_set_ban_errors},
        {"set_ban normalization + get_ban", test_set_and_get_bans},
        {"unbanned syscalls still work", test_unbanned_syscalls_work},
        {"block getpid", test_ban_getpid},
        {"block pipe", test_ban_pipe},
        {"block kill", test_ban_kill},
        {"block signal (rt_sigaction)", test_ban_signal},
        {"fork inheritance", test_fork_inheritance},
        {"check_ban behavior and error priority", test_check_ban},
        {"flip_ban_branch flips two ancestors", test_flip_ban_branch},
        {"flip_ban_branch errors", test_flip_errors},
    };

    int failed = 0;
    int skipped = 0;

    for (const TestCase& test : tests) {
        const int rc = run_isolated(test);
        failed += (rc == TEST_FAIL);
        skipped += (rc == TEST_SKIP);
    }

    std::printf("\nSummary: %d failed, %d skipped, %zu total\n",
                failed, skipped, sizeof(tests) / sizeof(tests[0]));

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
