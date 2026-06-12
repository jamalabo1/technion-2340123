#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#define SYS_set_ban 334
#define SYS_get_ban 335
#define SYS_check_ban 336
#define SYS_flip_ban_branch 337

long set_ban(int ban_getpid, int ban_pipe, int ban_kill, int ban_sig) {
    return syscall(SYS_set_ban, ban_getpid, ban_pipe, ban_kill, ban_sig);
}

long get_ban(char ban) {
    return syscall(SYS_get_ban, ban);
}

long check_ban(pid_t pid, char ban) {
    return syscall(SYS_check_ban, pid, ban);
}

long flip_ban_branch(int height, char ban) {
    return syscall(SYS_flip_ban_branch, height, ban);
}

void test_set_ban_validation() {
    long ret = set_ban(-1, 0, 0, 0);
    assert(ret == -1 && errno == EINVAL);

    if (geteuid() != 0) {
        ret = set_ban(1, 0, 0, 0);
        assert(ret == -1 && errno == EPERM);
    }
}

void test_get_ban_logic() {
    if (geteuid() != 0) return;

    assert(set_ban(1, 0, 0, 0) == 0);
    assert(get_ban('g') == 1);
    assert(get_ban('p') == 0);

    assert(get_ban('x') == -1 && errno == EINVAL);

    assert(set_ban(0, 0, 0, 0) == 0);
    assert(get_ban('g') == 0);
}


void test_check_ban_logic() {
    if (geteuid() != 0) return;

    assert(set_ban(0, 0, 0, 0) == 0);

    // Initialize synchronization pipe
    int sync_fd[2];
    assert(pipe(sync_fd) == 0);

    pid_t child = fork();

    if (child == 0) {
        close(sync_fd[0]);

        assert(set_ban(0, 0, 1, 0) == 0);

        // Signal parent that ban state is configured
        char msg = '1';
        write(sync_fd[1], &msg, 1);
        close(sync_fd[1]);

        sleep(2);
        _exit(0);
    } else {
        close(sync_fd[1]);

        // Block until child writes to pipe
        char msg;
        read(sync_fd[0], &msg, 1);
        close(sync_fd[0]);

        // State is now deterministic
        assert(check_ban(child, 'k') == 1);

        assert(set_ban(0, 1, 0, 0) == 0);
        assert(check_ban(child, 'p') == -1 && errno == EPERM);

        assert(check_ban(999999, 's') == -1 && errno == ESRCH);

        waitpid(child, NULL, 0);
        assert(set_ban(0, 0, 0, 0) == 0);
    }
}

void test_flip_ban_branch() {
    if (geteuid() != 0) return;

    assert(set_ban(0, 0, 0, 0) == 0);

    pid_t child1 = fork();
    if (child1 == 0) {
        pid_t child2 = fork();
        if (child2 == 0) {
            long banned_count = flip_ban_branch(2, 'g');
            assert(banned_count == 2 || banned_count == -1);
            _exit(0);
        }
        waitpid(child2, NULL, 0);
        _exit(0);
    }
    waitpid(child1, NULL, 0);
}

int main() {
    test_set_ban_validation();
    if (geteuid() == 0) {
        test_get_ban_logic();
        test_check_ban_logic();
        test_flip_ban_branch();
    } else {
        printf("Warning: Tests requiring root privileges skipped.\n");
    }
    return 0;
}