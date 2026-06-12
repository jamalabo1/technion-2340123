#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Assignment syscall numbers for the modified x86-64 kernel.
static constexpr long SYS_SET_BAN         = 334;
static constexpr long SYS_GET_BAN         = 335;
static constexpr long SYS_CHECK_BAN       = 336;
static constexpr long SYS_FLIP_BAN_BRANCH = 337;

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

enum TestExit {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 77,
};

static int fail(const char* what) {
    dprintf(STDERR_FILENO, "    %s (errno=%d: %s)\n",
            what, errno, strerror(errno));
    return TEST_FAIL;
}

static bool expect_value(long actual, long expected, const char* what) {
    if (actual == expected)
        return true;

    dprintf(STDERR_FILENO,
            "    %s: expected %ld, got %ld (errno=%d: %s)\n",
            what, expected, actual, errno, strerror(errno));
    return false;
}

static bool expect_ok(long actual, const char* what) {
    return expect_value(actual, 0, what);
}

static bool expect_errno(long actual, int expected_errno, const char* what) {
    if (actual == -1 && errno == expected_errno)
        return true;

    dprintf(STDERR_FILENO,
            "    %s: expected rc=-1, errno=%d; got rc=%ld, errno=%d (%s)\n",
            what, expected_errno, actual, errno, strerror(errno));
    return false;
}

static int require_root() {
    if (geteuid() == 0)
        return TEST_PASS;

    dprintf(STDERR_FILENO, "    requires root; run with sudo\n");
    return TEST_SKIP;
}

static bool check_mask(int g, int p, int k, int s, const char* context) {
    const std::array<char, 4> names{{'g', 'p', 'k', 's'}};
    const std::array<int, 4> expected{{g, p, k, s}};

    bool ok = true;
    for (size_t i = 0; i < names.size(); ++i) {
        const long rc = get_ban(names[i]);
        if (rc != expected[i]) {
            dprintf(STDERR_FILENO,
                    "    %s: get_ban('%c') expected %d, got %ld "
                    "(errno=%d: %s)\n",
                    context, names[i], expected[i], rc,
                    errno, strerror(errno));
            ok = false;
        }
    }
    return ok;
}

static bool write_full(int fd, const void* data, size_t size) {
    const char* cursor = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t written = write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        cursor += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

static bool read_full(int fd, void* data, size_t size) {
    char* cursor = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t received = read(fd, cursor, size);
        if (received == 0)
            return false;
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        cursor += received;
        size -= static_cast<size_t>(received);
    }
    return true;
}

static bool wait_for_exit(pid_t pid, int expected_exit = TEST_PASS) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == expected_exit;
}

static int count_open_fds() {
    DIR* directory = opendir("/proc/self/fd");
    if (directory == nullptr)
        return -1;

    int count = 0;
    errno = 0;
    while (dirent* entry = readdir(directory)) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            ++count;
        }
    }

    const int saved_errno = errno;
    if (closedir(directory) != 0)
        return -1;
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return count;
}

// ---------------------------------------------------------------------------
// set_ban / get_ban
// ---------------------------------------------------------------------------

static int test_set_ban_nonroot_and_error_priority() {
    if (geteuid() == 0 && setuid(65534) != 0)
        return fail("setuid(65534) failed");

    bool ok = true;
    ok &= expect_errno(set_ban(-1, 0, 0, 0), EINVAL,
                       "EINVAL must precede EPERM");
    ok &= expect_errno(set_ban(0, 0, 0, 0), EPERM,
                       "valid set_ban by non-root");
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_all_sixteen_ban_masks() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    for (int mask = 0; mask < 16; ++mask) {
        const int g = (mask >> 0) & 1;
        const int p = (mask >> 1) & 1;
        const int k = (mask >> 2) & 1;
        const int s = (mask >> 3) & 1;

        if (!expect_ok(set_ban(g, p, k, s), "set one of 16 masks"))
            return TEST_FAIL;
        if (!check_mask(g, p, k, s, "16-mask round trip"))
            return TEST_FAIL;
    }

    return TEST_PASS;
}

static int test_set_ban_normalizes_large_values() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    bool ok = true;
    ok &= expect_ok(set_ban(INT_MAX, 2, 17, 999999),
                    "positive values greater than one");
    ok &= check_mask(1, 1, 1, 1, "large values normalize to one");

    ok &= expect_ok(set_ban(0, INT_MAX, 0, 2),
                    "mixed zero and large values");
    ok &= check_mask(0, 1, 0, 1, "mixed normalization");
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_set_ban_negative_arguments_are_atomic() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    if (!expect_ok(set_ban(1, 0, 1, 0), "establish initial mask"))
        return TEST_FAIL;

    const int invalid[][4] = {
        {-1, 0, 0, 0},
        {0, -1, 0, 0},
        {0, 0, -1, 0},
        {0, 0, 0, -1},
        {INT_MIN, 0, 0, 0},
        {-1, -2, -3, -4},
        {INT_MAX, 0, -1, 2},
    };

    for (const auto& args : invalid) {
        if (!expect_errno(set_ban(args[0], args[1], args[2], args[3]),
                          EINVAL, "negative set_ban argument")) {
            return TEST_FAIL;
        }
        if (!check_mask(1, 0, 1, 0,
                        "failed set_ban must preserve the old mask")) {
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

static int test_invalid_get_ban_characters() {
    const std::array<char, 10> invalid{{
        '\0', 'G', 'P', 'K', 'S', 'x', '1', ' ', '\n',
        static_cast<char>(0xff)
    }};

    for (char value : invalid) {
        if (!expect_errno(get_ban(value), EINVAL,
                          "get_ban rejects invalid character")) {
            return TEST_FAIL;
        }
    }
    return TEST_PASS;
}

// ---------------------------------------------------------------------------
// Actual syscall blocking
// ---------------------------------------------------------------------------

static int test_each_bit_blocks_only_its_syscall() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t self = static_cast<pid_t>(syscall(SYS_getpid));
    if (self <= 0)
        return fail("initial raw getpid failed");

    for (int selected = 0; selected < 4; ++selected) {
        const int g = selected == 0;
        const int p = selected == 1;
        const int k = selected == 2;
        const int s = selected == 3;
        if (!expect_ok(set_ban(g, p, k, s), "set exactly one ban"))
            return TEST_FAIL;

        errno = 0;
        const long getpid_rc = syscall(SYS_getpid);
        if (g) {
            if (!expect_errno(getpid_rc, EPERM, "selected getpid is blocked"))
                return TEST_FAIL;
        } else if (getpid_rc != self) {
            dprintf(STDERR_FILENO,
                    "    unrelated getpid was affected: expected %d, got %ld\n",
                    self, getpid_rc);
            return TEST_FAIL;
        }

        int fds[2] = {-1, -1};
        errno = 0;
        const long pipe_rc = syscall(SYS_pipe, fds);
        if (p) {
            if (!expect_errno(pipe_rc, EPERM, "selected pipe is blocked"))
                return TEST_FAIL;
        } else {
            if (pipe_rc != 0)
                return fail("unrelated pipe was affected");
            close(fds[0]);
            close(fds[1]);
        }

        errno = 0;
        const long kill_rc = syscall(SYS_kill, self, 0);
        if (k) {
            if (!expect_errno(kill_rc, EPERM, "selected kill is blocked"))
                return TEST_FAIL;
        } else if (kill_rc != 0) {
            return fail("unrelated kill was affected");
        }

        errno = 0;
        const sighandler_t signal_rc = signal(SIGUSR2, SIG_IGN);
        if (s) {
            if (signal_rc != SIG_ERR || errno != EPERM) {
                dprintf(STDERR_FILENO,
                        "    selected signal expected SIG_ERR/EPERM, got %p "
                        "errno=%d (%s)\n",
                        reinterpret_cast<void*>(signal_rc),
                        errno, strerror(errno));
                return TEST_FAIL;
            }
        } else if (signal_rc == SIG_ERR) {
            return fail("unrelated signal/rt_sigaction was affected");
        }
    }

    return TEST_PASS;
}

static int test_blocked_pipe_has_no_side_effects() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const int before = count_open_fds();
    if (before < 0)
        return fail("count_open_fds before blocked pipes");

    if (!expect_ok(set_ban(0, 1, 0, 0), "ban pipe"))
        return TEST_FAIL;

    for (int i = 0; i < 128; ++i) {
        int fds[2] = {0x12345678, 0x23456789};
        if (!expect_errno(syscall(SYS_pipe, fds), EPERM,
                          "blocked pipe attempt")) {
            return TEST_FAIL;
        }
        if (fds[0] != 0x12345678 || fds[1] != 0x23456789) {
            dprintf(STDERR_FILENO,
                    "    blocked pipe modified userspace output: {%#x, %#x}\n",
                    fds[0], fds[1]);
            return TEST_FAIL;
        }
    }

    const int after = count_open_fds();
    if (after < 0)
        return fail("count_open_fds after blocked pipes");
    if (before != after) {
        dprintf(STDERR_FILENO,
                "    blocked pipe leaked descriptors: before=%d, after=%d\n",
                before, after);
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static volatile sig_atomic_t delivered_signals = 0;

static void counting_handler(int) {
    ++delivered_signals;
}

static int test_blocked_kill_does_not_deliver_signal() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    struct sigaction action {};
    action.sa_handler = counting_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr) != 0)
        return fail("install SIGUSR1 handler");

    const pid_t self = static_cast<pid_t>(syscall(SYS_getpid));
    if (self <= 0)
        return fail("getpid before banning kill");

    delivered_signals = 0;
    if (!expect_ok(set_ban(0, 0, 1, 0), "ban kill"))
        return TEST_FAIL;

    if (!expect_errno(syscall(SYS_kill, self, SIGUSR1), EPERM,
                      "blocked kill(SIGUSR1)")) {
        return TEST_FAIL;
    }

    const timespec delay{0, 20 * 1000 * 1000};
    nanosleep(&delay, nullptr);
    if (delivered_signals != 0) {
        dprintf(STDERR_FILENO,
                "    blocked kill delivered %d signal(s) before returning\n",
                static_cast<int>(delivered_signals));
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static void handler_a(int) {}
static void handler_b(int) {}

static int test_blocked_signal_does_not_change_handler() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    struct sigaction initial {};
    initial.sa_handler = handler_a;
    sigemptyset(&initial.sa_mask);
    if (sigaction(SIGUSR1, &initial, nullptr) != 0)
        return fail("install initial signal handler");

    if (!expect_ok(set_ban(0, 0, 0, 1), "ban signal"))
        return TEST_FAIL;

    errno = 0;
    if (signal(SIGUSR1, handler_b) != SIG_ERR || errno != EPERM) {
        dprintf(STDERR_FILENO,
                "    blocked signal did not return SIG_ERR/EPERM "
                "(errno=%d: %s)\n",
                errno, strerror(errno));
        return TEST_FAIL;
    }

    // Querying with sigaction also uses rt_sigaction, so first lift the ban.
    if (!expect_ok(set_ban(0, 0, 0, 0), "lift signal ban"))
        return TEST_FAIL;

    struct sigaction observed {};
    if (sigaction(SIGUSR1, nullptr, &observed) != 0)
        return fail("query signal handler");
    if (observed.sa_handler != handler_a) {
        dprintf(STDERR_FILENO,
                "    blocked signal changed the installed handler\n");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

// ---------------------------------------------------------------------------
// fork / exec inheritance
// ---------------------------------------------------------------------------

static int test_fork_inheritance_snapshot_and_independence() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    int child_ready[2];
    int parent_changed[2];
    if (pipe(child_ready) != 0 || pipe(parent_changed) != 0)
        return fail("create synchronization pipes");

    if (!expect_ok(set_ban(1, 0, 1, 0), "set parent pre-fork mask"))
        return TEST_FAIL;

    const pid_t child = fork();
    if (child < 0)
        return fail("fork inheritance child");

    if (child == 0) {
        close(child_ready[0]);
        close(parent_changed[1]);

        if (!check_mask(1, 0, 1, 0, "child inherited fork snapshot"))
            _exit(TEST_FAIL);

        const char ready = 'R';
        if (!write_full(child_ready[1], &ready, 1))
            _exit(TEST_FAIL);

        char changed = 0;
        if (!read_full(parent_changed[0], &changed, 1))
            _exit(TEST_FAIL);

        if (!check_mask(1, 0, 1, 0,
                        "parent changes do not mutate child state")) {
            _exit(TEST_FAIL);
        }

        if (set_ban(0, 1, 0, 1) != 0)
            _exit(TEST_FAIL);
        if (!check_mask(0, 1, 0, 1, "child can mutate its own copy"))
            _exit(TEST_FAIL);

        _exit(TEST_PASS);
    }

    close(child_ready[1]);
    close(parent_changed[0]);

    char ready = 0;
    if (!read_full(child_ready[0], &ready, 1))
        return fail("wait for child snapshot check");

    if (!expect_ok(set_ban(0, 1, 1, 1), "change parent after fork"))
        return TEST_FAIL;
    const char changed = 'C';
    if (!write_full(parent_changed[1], &changed, 1))
        return fail("notify child about parent change");

    const bool child_ok = wait_for_exit(child);
    const bool parent_ok = check_mask(0, 1, 1, 1,
                                      "child changes do not mutate parent state");
    return child_ok && parent_ok ? TEST_PASS : TEST_FAIL;
}

static int exec_probe_main(int argc, char** argv) {
    if (argc != 6)
        return TEST_FAIL;

    const int g = atoi(argv[2]);
    const int p = atoi(argv[3]);
    const int k = atoi(argv[4]);
    const int s = atoi(argv[5]);
    return check_mask(g, p, k, s, "mask persisted across exec")
               ? TEST_PASS : TEST_FAIL;
}

static int test_exec_preserves_bans() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    char executable[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", executable,
                                    sizeof(executable) - 1);
    if (length < 0)
        return fail("readlink /proc/self/exe");
    executable[length] = '\0';

    if (!expect_ok(set_ban(1, 1, 1, 1), "set all bans before exec"))
        return TEST_FAIL;

    execl(executable, executable, "--exec-probe",
          "1", "1", "1", "1", static_cast<char*>(nullptr));
    return fail("exec self failed");
}

// ---------------------------------------------------------------------------
// check_ban
// ---------------------------------------------------------------------------

static int test_check_ban_self() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t self = static_cast<pid_t>(syscall(SYS_getpid));
    if (self <= 0)
        return fail("get self pid");

    if (!expect_ok(set_ban(1, 0, 1, 0), "set self mask"))
        return TEST_FAIL;

    bool ok = true;
    // A process cannot check a ban that it itself currently has.
    ok &= expect_errno(check_ban(self, 'g'), EPERM,
                       "self-check of banned getpid");
    ok &= expect_value(check_ban(self, 'p'), 0,
                       "self-check of unbanned pipe");
    ok &= expect_errno(check_ban(self, 'k'), EPERM,
                       "self-check of banned kill");
    ok &= expect_value(check_ban(self, 's'), 0,
                       "self-check of unbanned signal");
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_check_ban_other_process_all_flags() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    int ready_pipe[2];
    int stop_pipe[2];
    if (pipe(ready_pipe) != 0 || pipe(stop_pipe) != 0)
        return fail("create target synchronization pipes");

    const pid_t target = fork();
    if (target < 0)
        return fail("fork check_ban target");

    if (target == 0) {
        close(ready_pipe[0]);
        close(stop_pipe[1]);
        if (set_ban(1, 1, 1, 1) != 0)
            _exit(TEST_FAIL);
        const char ready = 'R';
        if (!write_full(ready_pipe[1], &ready, 1))
            _exit(TEST_FAIL);
        char stop = 0;
        if (!read_full(stop_pipe[0], &stop, 1))
            _exit(TEST_FAIL);
        _exit(TEST_PASS);
    }

    close(ready_pipe[1]);
    close(stop_pipe[0]);
    char ready = 0;
    if (!read_full(ready_pipe[0], &ready, 1))
        return fail("wait for check_ban target");

    bool ok = true;
    for (char ban : std::array<char, 4>{{'g', 'p', 'k', 's'}})
        ok &= expect_value(check_ban(target, ban), 1,
                           "target has requested ban");

    const char stop = 'S';
    ok &= write_full(stop_pipe[1], &stop, 1);
    ok &= wait_for_exit(target);
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_check_ban_does_not_require_root() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    int ready_pipe[2];
    int stop_pipe[2];
    if (pipe(ready_pipe) != 0 || pipe(stop_pipe) != 0)
        return fail("create non-root check pipes");

    const pid_t target = fork();
    if (target < 0)
        return fail("fork privileged target");

    if (target == 0) {
        close(ready_pipe[0]);
        close(stop_pipe[1]);
        if (set_ban(1, 0, 0, 0) != 0)
            _exit(TEST_FAIL);
        const char ready = 'R';
        if (!write_full(ready_pipe[1], &ready, 1))
            _exit(TEST_FAIL);
        char stop = 0;
        if (!read_full(stop_pipe[0], &stop, 1))
            _exit(TEST_FAIL);
        _exit(TEST_PASS);
    }

    close(ready_pipe[1]);
    close(stop_pipe[0]);
    char ready = 0;
    if (!read_full(ready_pipe[0], &ready, 1))
        return fail("wait for privileged target");

    if (setuid(65534) != 0)
        return fail("drop privileges before check_ban");

    bool ok = true;
    ok &= expect_value(check_ban(target, 'g'), 1,
                       "non-root caller may inspect target");
    ok &= expect_value(check_ban(target, 'p'), 0,
                       "non-root caller sees unset target ban");

    const char stop = 'S';
    ok &= write_full(stop_pipe[1], &stop, 1);
    ok &= wait_for_exit(target);
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_check_ban_invalid_and_missing_pids() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    if (!expect_ok(set_ban(0, 0, 0, 0), "clear caller bans"))
        return TEST_FAIL;

    const pid_t child = fork();
    if (child < 0)
        return fail("fork soon-dead child");
    if (child == 0)
        _exit(TEST_PASS);
    if (!wait_for_exit(child))
        return TEST_FAIL;

    bool ok = true;
    ok &= expect_errno(check_ban(child, 'g'), ESRCH,
                       "reaped child no longer exists");
    ok &= expect_errno(check_ban(0, 'g'), ESRCH,
                       "pid zero is not a task pid for check_ban");
    ok &= expect_errno(check_ban(-1, 'g'), ESRCH,
                       "negative pid does not exist");
    ok &= expect_errno(check_ban(INT_MAX, 'g'), ESRCH,
                       "impossible large pid does not exist");

    for (char invalid : std::array<char, 5>{{'x', 'G', '\0', '1', ' '}}) {
        ok &= expect_errno(check_ban(INT_MAX, invalid), EINVAL,
                           "invalid ban precedes missing pid");
    }
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_check_ban_error_priority() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    int stop_pipe[2];
    if (pipe(stop_pipe) != 0)
        return fail("create priority target pipe");

    const pid_t target = fork();
    if (target < 0)
        return fail("fork priority target");
    if (target == 0) {
        close(stop_pipe[1]);
        char stop = 0;
        if (!read_full(stop_pipe[0], &stop, 1))
            _exit(TEST_FAIL);
        _exit(TEST_PASS);
    }

    close(stop_pipe[0]);
    if (!expect_ok(set_ban(1, 0, 0, 0), "ban caller from getpid"))
        return TEST_FAIL;

    bool ok = true;
    ok &= expect_errno(check_ban(INT_MAX, 'x'), EINVAL,
                       "EINVAL precedes ESRCH and EPERM");
    ok &= expect_errno(check_ban(INT_MAX, 'g'), ESRCH,
                       "ESRCH precedes caller EPERM");
    ok &= expect_errno(check_ban(target, 'g'), EPERM,
                       "existing target reaches caller-ban check");

    const char stop = 'S';
    ok &= write_full(stop_pipe[1], &stop, 1);
    ok &= wait_for_exit(target);
    return ok ? TEST_PASS : TEST_FAIL;
}

// ---------------------------------------------------------------------------
// flip_ban_branch
// ---------------------------------------------------------------------------

struct OneLevelReport {
    long flip_result;
    int flip_errno;
    long caller_ban;
    long parent_ban;
};

static int test_flip_all_four_ban_types_one_level() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t parent_pid = static_cast<pid_t>(syscall(SYS_getpid));
    if (parent_pid <= 0)
        return fail("capture parent pid");

    for (char ban : std::array<char, 4>{{'g', 'p', 'k', 's'}}) {
        if (!expect_ok(set_ban(0, 0, 0, 0), "clear parent before one-level flip"))
            return TEST_FAIL;

        int report_pipe[2];
        if (pipe(report_pipe) != 0)
            return fail("create one-level report pipe");

        const pid_t child = fork();
        if (child < 0)
            return fail("fork one-level flipper");

        if (child == 0) {
            close(report_pipe[0]);
            OneLevelReport report{};
            report.flip_result = flip_ban_branch(1, ban);
            report.flip_errno = errno;
            report.caller_ban = get_ban(ban);
            report.parent_ban = check_ban(parent_pid, ban);
            const bool sent = write_full(report_pipe[1], &report, sizeof(report));
            _exit(sent ? TEST_PASS : TEST_FAIL);
        }

        close(report_pipe[1]);
        OneLevelReport report{};
        const bool received = read_full(report_pipe[0], &report, sizeof(report));
        close(report_pipe[0]);
        const bool child_ok = wait_for_exit(child);

        if (!received || !child_ok || report.flip_result != 1 ||
            report.caller_ban != 0 || report.parent_ban != 1) {
            dprintf(STDERR_FILENO,
                    "    one-level flip '%c' failed: received=%d child=%d "
                    "rc=%ld errno=%d caller=%ld parent=%ld\n",
                    ban, received, child_ok, report.flip_result,
                    report.flip_errno, report.caller_ban, report.parent_ban);
            return TEST_FAIL;
        }

        const int expected_g = ban == 'g';
        const int expected_p = ban == 'p';
        const int expected_k = ban == 'k';
        const int expected_s = ban == 's';
        if (!check_mask(expected_g, expected_p, expected_k, expected_s,
                        "one-level flip changed exactly one parent bit")) {
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

struct HeightReport {
    long flip_result;
    int flip_errno;
    long caller_ban;
    long parent_ban;
    long grandparent_ban;
};

static int test_flip_height_one_does_not_touch_grandparent() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t grandparent_pid = static_cast<pid_t>(syscall(SYS_getpid));
    if (grandparent_pid <= 0)
        return fail("capture grandparent pid");
    if (!expect_ok(set_ban(0, 0, 0, 0), "clear grandparent mask"))
        return TEST_FAIL;

    int report_pipe[2];
    if (pipe(report_pipe) != 0)
        return fail("create height report pipe");

    const pid_t parent = fork();
    if (parent < 0)
        return fail("fork middle parent");

    if (parent == 0) {
        close(report_pipe[0]);
        if (set_ban(0, 0, 0, 0) != 0)
            _exit(TEST_FAIL);
        const pid_t parent_pid = static_cast<pid_t>(syscall(SYS_getpid));
        if (parent_pid <= 0)
            _exit(TEST_FAIL);

        const pid_t caller = fork();
        if (caller < 0)
            _exit(TEST_FAIL);
        if (caller == 0) {
            HeightReport report{};
            report.flip_result = flip_ban_branch(1, 'g');
            report.flip_errno = errno;
            report.caller_ban = get_ban('g');
            report.parent_ban = check_ban(parent_pid, 'g');
            report.grandparent_ban = check_ban(grandparent_pid, 'g');
            const bool sent = write_full(report_pipe[1], &report, sizeof(report));
            _exit(sent ? TEST_PASS : TEST_FAIL);
        }

        _exit(wait_for_exit(caller) ? TEST_PASS : TEST_FAIL);
    }

    close(report_pipe[1]);
    HeightReport report{};
    const bool received = read_full(report_pipe[0], &report, sizeof(report));
    close(report_pipe[0]);
    const bool parent_ok = wait_for_exit(parent);

    if (!received || !parent_ok || report.flip_result != 1 ||
        report.caller_ban != 0 || report.parent_ban != 1 ||
        report.grandparent_ban != 0 || get_ban('g') != 0) {
        dprintf(STDERR_FILENO,
                "    exact-height failure: received=%d parent_ok=%d rc=%ld "
                "errno=%d caller=%ld parent=%ld grandparent=%ld self=%ld\n",
                received, parent_ok, report.flip_result, report.flip_errno,
                report.caller_ban, report.parent_ban,
                report.grandparent_ban, get_ban('g'));
        return TEST_FAIL;
    }

    return TEST_PASS;
}

struct MixedFlipReport {
    long first_result;
    int first_errno;
    long first_parent[3];
    long second_result;
    int second_errno;
    long second_parent[3];
    long caller_ban;
};

static int test_flip_mixed_three_level_branch_twice() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t great_grandparent_pid =
        static_cast<pid_t>(syscall(SYS_getpid));
    if (great_grandparent_pid <= 0)
        return fail("capture great-grandparent pid");
    if (!expect_ok(set_ban(1, 0, 0, 0),
                   "great-grandparent starts banned")) {
        return TEST_FAIL;
    }

    int report_pipe[2];
    if (pipe(report_pipe) != 0)
        return fail("create mixed-branch report pipe");

    const pid_t grandparent = fork();
    if (grandparent < 0)
        return fail("fork grandparent");

    if (grandparent == 0) {
        close(report_pipe[0]);

        // Inherited g=1; make this level g=0 before obtaining its pid.
        if (set_ban(0, 0, 0, 0) != 0)
            _exit(TEST_FAIL);
        const pid_t grandparent_pid =
            static_cast<pid_t>(syscall(SYS_getpid));
        if (grandparent_pid <= 0)
            _exit(TEST_FAIL);

        const pid_t parent = fork();
        if (parent < 0)
            _exit(TEST_FAIL);
        if (parent == 0) {
            const pid_t parent_pid =
                static_cast<pid_t>(syscall(SYS_getpid));
            if (parent_pid <= 0)
                _exit(TEST_FAIL);
            if (set_ban(1, 0, 0, 0) != 0)
                _exit(TEST_FAIL);

            const pid_t caller = fork();
            if (caller < 0)
                _exit(TEST_FAIL);
            if (caller == 0) {
                // Inherited g=1; caller must be unbanned to invoke/check g.
                if (set_ban(0, 0, 0, 0) != 0)
                    _exit(TEST_FAIL);

                MixedFlipReport report{};
                report.first_result = flip_ban_branch(3, 'g');
                report.first_errno = errno;
                report.first_parent[0] = check_ban(parent_pid, 'g');
                report.first_parent[1] = check_ban(grandparent_pid, 'g');
                report.first_parent[2] =
                    check_ban(great_grandparent_pid, 'g');

                report.second_result = flip_ban_branch(3, 'g');
                report.second_errno = errno;
                report.second_parent[0] = check_ban(parent_pid, 'g');
                report.second_parent[1] = check_ban(grandparent_pid, 'g');
                report.second_parent[2] =
                    check_ban(great_grandparent_pid, 'g');
                report.caller_ban = get_ban('g');

                const bool sent =
                    write_full(report_pipe[1], &report, sizeof(report));
                _exit(sent ? TEST_PASS : TEST_FAIL);
            }

            _exit(wait_for_exit(caller) ? TEST_PASS : TEST_FAIL);
        }

        _exit(wait_for_exit(parent) ? TEST_PASS : TEST_FAIL);
    }

    close(report_pipe[1]);
    MixedFlipReport report{};
    const bool received = read_full(report_pipe[0], &report, sizeof(report));
    close(report_pipe[0]);
    const bool chain_ok = wait_for_exit(grandparent);

    const bool values_ok =
        report.first_result == 1 &&
        report.first_parent[0] == 0 &&
        report.first_parent[1] == 1 &&
        report.first_parent[2] == 0 &&
        report.second_result == 2 &&
        report.second_parent[0] == 1 &&
        report.second_parent[1] == 0 &&
        report.second_parent[2] == 1 &&
        report.caller_ban == 0;

    if (!received || !chain_ok || !values_ok || get_ban('g') != 1) {
        dprintf(STDERR_FILENO,
                "    mixed flip failed: received=%d chain=%d "
                "first=%ld [%ld,%ld,%ld] second=%ld [%ld,%ld,%ld] "
                "caller=%ld top=%ld\n",
                received, chain_ok,
                report.first_result,
                report.first_parent[0], report.first_parent[1],
                report.first_parent[2],
                report.second_result,
                report.second_parent[0], report.second_parent[1],
                report.second_parent[2],
                report.caller_ban, get_ban('g'));
        return TEST_FAIL;
    }

    return TEST_PASS;
}

struct AlreadyBannedReport {
    long flip_result;
    int flip_errno;
    long caller_ban;
    long parent_ban;
};

static int test_flip_unbans_parent_and_returns_zero() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    const pid_t parent_pid = static_cast<pid_t>(syscall(SYS_getpid));
    if (parent_pid <= 0)
        return fail("capture parent pid before pipe ban");
    if (!expect_ok(set_ban(0, 1, 0, 0), "parent starts pipe-banned"))
        return TEST_FAIL;

    // Use a socket-free report pipe created before the parent was pipe-banned.
    // The pipe above cannot be created now, so temporarily lift, create, restore.
    if (!expect_ok(set_ban(0, 0, 0, 0), "temporarily lift pipe ban"))
        return TEST_FAIL;
    int report_pipe[2];
    if (pipe(report_pipe) != 0)
        return fail("create already-banned report pipe");
    if (!expect_ok(set_ban(0, 1, 0, 0), "restore parent pipe ban"))
        return TEST_FAIL;

    const pid_t child = fork();
    if (child < 0)
        return fail("fork unbanning child");
    if (child == 0) {
        close(report_pipe[0]);
        // Child inherited p=1; lift only its own ban before invoking flip.
        if (set_ban(0, 0, 0, 0) != 0)
            _exit(TEST_FAIL);

        AlreadyBannedReport report{};
        report.flip_result = flip_ban_branch(1, 'p');
        report.flip_errno = errno;
        report.caller_ban = get_ban('p');
        report.parent_ban = check_ban(parent_pid, 'p');
        const bool sent = write_full(report_pipe[1], &report, sizeof(report));
        _exit(sent ? TEST_PASS : TEST_FAIL);
    }

    close(report_pipe[1]);
    AlreadyBannedReport report{};
    const bool received = read_full(report_pipe[0], &report, sizeof(report));
    close(report_pipe[0]);
    const bool child_ok = wait_for_exit(child);

    if (!received || !child_ok || report.flip_result != 0 ||
        report.caller_ban != 0 || report.parent_ban != 0 ||
        get_ban('p') != 0) {
        dprintf(STDERR_FILENO,
                "    unban-parent flip failed: received=%d child=%d rc=%ld "
                "errno=%d caller=%ld parent=%ld self=%ld\n",
                received, child_ok, report.flip_result,
                report.flip_errno, report.caller_ban,
                report.parent_ban, get_ban('p'));
        return TEST_FAIL;
    }

    return TEST_PASS;
}

static int test_flip_errors_and_priority() {
    if (require_root() != TEST_PASS)
        return TEST_SKIP;

    if (!expect_ok(set_ban(0, 0, 0, 0), "clear caller bans"))
        return TEST_FAIL;

    bool ok = true;
    ok &= expect_errno(flip_ban_branch(0, 'g'), EINVAL, "height zero");
    ok &= expect_errno(flip_ban_branch(-1, 'g'), EINVAL, "negative height");
    ok &= expect_errno(flip_ban_branch(INT_MIN, 'g'), EINVAL,
                       "INT_MIN height");
    ok &= expect_errno(flip_ban_branch(1, 'x'), EINVAL,
                       "invalid ban character");
    ok &= expect_errno(flip_ban_branch(0, 'x'), EINVAL,
                       "both arguments invalid");

    ok &= expect_ok(set_ban(1, 0, 0, 0), "ban caller from getpid");
    ok &= expect_errno(flip_ban_branch(0, 'g'), EINVAL,
                       "invalid height precedes caller EPERM");
    ok &= expect_errno(flip_ban_branch(1, 'x'), EINVAL,
                       "invalid character precedes caller state");
    ok &= expect_errno(flip_ban_branch(1, 'g'), EPERM,
                       "valid call reaches caller-ban check");
    return ok ? TEST_PASS : TEST_FAIL;
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

struct TestCase {
    const char* name;
    int (*function)();
};

static int run_isolated(const TestCase& test) {
    const pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return TEST_FAIL;
    }

    if (child == 0) {
        // Put each test and its descendants in a separate process group so a
        // timeout can clean up an entire intentionally-created process chain.
        (void)setpgid(0, 0);
        _exit(test.function());
    }

    (void)setpgid(child, child);

    int status = 0;
    constexpr int timeout_ms = 8000;
    constexpr int poll_ms = 10;
    int elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        const pid_t rc = waitpid(child, &status, WNOHANG);
        if (rc == child)
            break;
        if (rc < 0) {
            perror("waitpid");
            return TEST_FAIL;
        }

        const timespec delay{0, poll_ms * 1000 * 1000};
        nanosleep(&delay, nullptr);
        elapsed_ms += poll_ms;
    }

    if (elapsed_ms >= timeout_ms) {
        (void)kill(-child, SIGKILL);
        (void)kill(child, SIGKILL);
        (void)waitpid(child, &status, 0);
        std::printf("[FAIL] %s (timeout)\n", test.name);
        return TEST_FAIL;
    }

    if (!WIFEXITED(status)) {
        std::printf("[FAIL] %s (terminated by signal %d)\n",
                    test.name,
                    WIFSIGNALED(status) ? WTERMSIG(status) : -1);
        return TEST_FAIL;
    }

    const int result = WEXITSTATUS(status);
    if (result == TEST_PASS)
        std::printf("[PASS] %s\n", test.name);
    else if (result == TEST_SKIP)
        std::printf("[SKIP] %s\n", test.name);
    else
        std::printf("[FAIL] %s\n", test.name);

    return result;
}

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--exec-probe") == 0)
        return exec_probe_main(argc, argv);

    const TestCase tests[] = {
        {"set_ban: non-root and error priority",
         test_set_ban_nonroot_and_error_priority},
        {"set/get: all 16 masks", test_all_sixteen_ban_masks},
        {"set_ban: normalize values > 1",
         test_set_ban_normalizes_large_values},
        {"set_ban: negative arguments are atomic",
         test_set_ban_negative_arguments_are_atomic},
        {"get_ban: invalid characters", test_invalid_get_ban_characters},
        {"blocking: each bit affects only its syscall",
         test_each_bit_blocks_only_its_syscall},
        {"blocking: pipe has no side effects",
         test_blocked_pipe_has_no_side_effects},
        {"blocking: kill does not deliver signal",
         test_blocked_kill_does_not_deliver_signal},
        {"blocking: signal does not change handler",
         test_blocked_signal_does_not_change_handler},
        {"fork: inheritance snapshot and independence",
         test_fork_inheritance_snapshot_and_independence},
        {"exec: bans persist", test_exec_preserves_bans},
        {"check_ban: self", test_check_ban_self},
        {"check_ban: other process, all flags",
         test_check_ban_other_process_all_flags},
        {"check_ban: non-root caller",
         test_check_ban_does_not_require_root},
        {"check_ban: invalid and missing PIDs",
         test_check_ban_invalid_and_missing_pids},
        {"check_ban: error priority", test_check_ban_error_priority},
        {"flip: all four ban types, height one",
         test_flip_all_four_ban_types_one_level},
        {"flip: height one excludes grandparent",
         test_flip_height_one_does_not_touch_grandparent},
        {"flip: mixed three-level branch twice",
         test_flip_mixed_three_level_branch_twice},
        {"flip: unban parent returns zero",
         test_flip_unbans_parent_and_returns_zero},
        {"flip: invalid arguments and error priority",
         test_flip_errors_and_priority},
    };

    int failed = 0;
    int skipped = 0;
    for (const TestCase& test : tests) {
        const int result = run_isolated(test);
        failed += result == TEST_FAIL;
        skipped += result == TEST_SKIP;
    }

    const size_t total = sizeof(tests) / sizeof(tests[0]);
    std::printf("\nSummary: %d failed, %d skipped, %zu total\n",
                failed, skipped, total);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
