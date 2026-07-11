// Comprehensive tests for Part 1 - naive smalloc (malloc_1.cpp).
//
// Spec (HW4-wet):
//   void* smalloc(size_t size):
//     - returns NULL if size == 0
//     - returns NULL if size > 10^8
//     - returns NULL if sbrk() fails
//     - on success returns a pointer to the first allocated byte
//       (i.e. the PREVIOUS program break, not the new one)
//
// Isolation: each test runs in its own fork()ed child process, so every test
// starts with a fresh, untouched heap and cannot see another test's data.
// A crashing test is reported (signal) and the suite keeps going.

#include <iostream>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "os_malloc.h"

#define MAX_ALLOC 100000000ULL // 10^8

static int g_failed = 0;  // per-child failure count

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::cout << "\n    CHECK FAILED (line " << __LINE__ << "): "    \
                      << #cond << std::flush;                                \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        size_t _va = (size_t)(a), _vb = (size_t)(b);                         \
        if (_va != _vb) {                                                    \
            ++g_failed;                                                      \
            std::cout << "\n    CHECK_EQ FAILED (line " << __LINE__ << "): " \
                      << #a " = " << _va << ", " #b " = " << _vb             \
                      << std::flush;                                         \
        }                                                                    \
    } while (0)

// fatal - the rest of THIS test would dereference garbage; aborts the child
#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cout << "\n    FATAL (line " << __LINE__ << "): " << #cond  \
                      << std::flush;                                         \
            _exit(255);                                                      \
        }                                                                    \
    } while (0)

static void fill_pattern(void *p, size_t n, unsigned char seed) {
    unsigned char *c = (unsigned char *) p;
    for (size_t i = 0; i < n; i++) c[i] = (unsigned char) (seed + i * 17);
}

static bool pattern_ok(const void *p, size_t n, unsigned char seed) {
    const unsigned char *c = (const unsigned char *) p;
    for (size_t i = 0; i < n; i++) {
        if (c[i] != (unsigned char) (seed + i * 17)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

static void test_rejects() {
    CHECK(smalloc(0) == NULL);
    CHECK(smalloc(MAX_ALLOC + 1) == NULL);
    CHECK(smalloc((size_t) -1) == NULL); // SIZE_MAX
}

static void test_basic_write() {
    char *p = (char *) smalloc(128);
    REQUIRE(p != NULL);
    fill_pattern(p, 128, 3);
    CHECK(pattern_ok(p, 128, 3));
}

// smalloc must return the PREVIOUS program break, so back-to-back
// allocations are contiguous: next pointer == prev pointer + prev size.
// (no printing between the allocations - stdio may sbrk internally)
static void test_prev_break_semantics() {
    char *p1 = (char *) smalloc(100);
    char *p2 = (char *) smalloc(200);
    char *p3 = (char *) smalloc(56);
    REQUIRE(p1 != NULL && p2 != NULL && p3 != NULL);
    CHECK_EQ(p2 - p1, 100);
    CHECK_EQ(p3 - p2, 200);
}

static void test_no_overlap() {
    const int N = 16;
    const size_t SZ = 64;
    char *ptrs[N];
    for (int i = 0; i < N; i++) {
        ptrs[i] = (char *) smalloc(SZ);
        REQUIRE(ptrs[i] != NULL);
        fill_pattern(ptrs[i], SZ, (unsigned char) (i * 7 + 1));
    }
    // verify only after ALL blocks were written - catches overlaps
    for (int i = 0; i < N; i++) {
        CHECK(pattern_ok(ptrs[i], SZ, (unsigned char) (i * 7 + 1)));
    }
}

static void test_int_array() {
    int *arr = (int *) smalloc(100 * sizeof(int));
    REQUIRE(arr != NULL);
    for (int i = 0; i < 100; i++) arr[i] = i * i - 3;
    for (int i = 0; i < 100; i++) CHECK(arr[i] == i * i - 3);
}

static void test_one_byte_allocations() {
    char *a = (char *) smalloc(1);
    char *b = (char *) smalloc(1);
    char *c = (char *) smalloc(1);
    REQUIRE(a != NULL && b != NULL && c != NULL);
    CHECK(a != b && b != c && a != c);
    *a = 'x'; *b = 'y'; *c = 'z';
    CHECK(*a == 'x' && *b == 'y' && *c == 'z');
}

// exactly 10^8 must SUCCEED (only strictly-more is rejected)
static void test_max_boundary() {
    char *p = (char *) smalloc(MAX_ALLOC);
    REQUIRE(p != NULL);
    p[0] = 'a';
    p[MAX_ALLOC - 1] = 'z';
    CHECK(p[0] == 'a');
    CHECK(p[MAX_ALLOC - 1] == 'z');
}

// ---------------------------------------------------------------------------

struct TestCase {
    const char *name;
    void (*fn)();
};

// runs one test in a forked child so it gets a pristine heap
static bool run_test(const TestCase &t) {
    std::cout << t.name << "... " << std::flush;
    pid_t pid = fork();
    if (pid < 0) { // fork failed - run inline as a fallback
        g_failed = 0;
        t.fn();
        std::cout << (g_failed == 0 ? "OK" : "\n    -> FAILED") << std::endl;
        return g_failed == 0;
    }
    if (pid == 0) {
        g_failed = 0;
        t.fn();
        _exit(g_failed > 254 ? 254 : g_failed);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        int fails = WEXITSTATUS(status);
        if (fails == 0) {
            std::cout << "OK" << std::endl;
            return true;
        }
        if (fails == 255) {
            std::cout << "\n    -> FATAL, test aborted" << std::endl;
            return false;
        }
        std::cout << "\n    -> " << fails << " check(s) failed" << std::endl;
        return false;
    }
    if (WIFSIGNALED(status)) {
        std::cout << "\n    -> CRASHED with signal " << WTERMSIG(status)
                  << (WTERMSIG(status) == SIGSEGV ? " (SIGSEGV)" : "")
                  << std::endl;
        return false;
    }
    std::cout << "\n    -> unknown termination" << std::endl;
    return false;
}

int main() {
    std::cout << "malloc_1 comprehensive tests (each test in its own process):"
              << std::endl;

    const TestCase tests[] = {
        {"Test 1: size 0 / size > 10^8 rejection",              test_rejects},
        {"Test 2: basic allocation is writable",                test_basic_write},
        {"Test 3: returns previous program break (contiguity)", test_prev_break_semantics},
        {"Test 4: many blocks do not overlap",                  test_no_overlap},
        {"Test 5: typed (int) array usage",                     test_int_array},
        {"Test 6: 1-byte allocations",                          test_one_byte_allocations},
        {"Test 7: exact 10^8 boundary succeeds",                test_max_boundary},
    };
    const int total = sizeof(tests) / sizeof(tests[0]);

    int passed = 0;
    for (int i = 0; i < total; i++) {
        if (run_test(tests[i])) passed++;
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << passed << " / " << total << " TESTS PASSED" << std::endl;
    return passed == total ? 0 : 1;
}
