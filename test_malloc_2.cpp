// Comprehensive tests for Part 2 - basic malloc with free-list (malloc_2.cpp).
//
// Spec highlights being tested (HW4-wet, part 2):
//   - smalloc/scalloc/srealloc reject size 0 and size > 10^8
//   - freed blocks are reused; search is in ASCENDING address order (first fit)
//   - a block's `size` field never changes: reusing a big block for a small
//     request marks the WHOLE block as used
//   - sfree(NULL) and double-free are no-ops
//   - scalloc zeroes the block (also when reusing a dirty freed block)
//   - srealloc: size <= block size => same pointer; otherwise find/allocate,
//     copy (memmove), free old.  oldp==NULL acts like smalloc.  On failure
//     (incl. size==0) oldp is NOT freed.
//   - stats functions 5-10
//
// Isolation: the allocator never resets - blocks keep their size forever and
// the heap only grows - so tests would otherwise inherit each other's blocks.
// To avoid that, each test runs in its own fork()ed child process: every test
// starts with a virgin heap (zero allocations), so all stats are absolute and
// pointer-reuse assertions are deterministic.  A crashing test is reported
// (signal) and the suite keeps going.

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

struct Stats {
    size_t free_blocks, free_bytes, alloc_blocks, alloc_bytes, meta_bytes;
    static Stats now() {
        Stats s;
        s.free_blocks  = _num_free_blocks();
        s.free_bytes   = _num_free_bytes();
        s.alloc_blocks = _num_allocated_blocks();
        s.alloc_bytes  = _num_allocated_bytes();
        s.meta_bytes   = _num_meta_data_bytes();
        return s;
    }
};

static size_t META;

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
// every test starts on a virgin heap: all stats are 0 at entry

static void test_rejects() {
    CHECK(smalloc(0) == NULL);
    CHECK(smalloc(MAX_ALLOC + 1) == NULL);
    CHECK(scalloc(0, 8) == NULL);
    CHECK(scalloc(8, 0) == NULL);
    CHECK(scalloc(MAX_ALLOC, 2) == NULL);          // 2*10^8 > 10^8
    CHECK(scalloc(MAX_ALLOC / 4 + 1, 4) == NULL);  // just above 10^8
    // num*size overflows size_t and wraps around to 2 - must still be NULL
    CHECK(scalloc((size_t)1 << (sizeof(size_t) * 8 - 1), 2) == NULL);
    CHECK(scalloc(((size_t)1 << (sizeof(size_t) * 8 - 1)) + 1, 2) == NULL);
    CHECK(srealloc(NULL, 0) == NULL);
    CHECK(srealloc(NULL, MAX_ALLOC + 1) == NULL);

    // rejected calls must not have allocated anything
    CHECK_EQ(_num_allocated_blocks(), 0);
    CHECK_EQ(_num_allocated_bytes(), 0);
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_free_bytes(), 0);
    CHECK_EQ(_num_meta_data_bytes(), 0);
}

static void test_alloc_stats() {
    char *p = (char *) smalloc(16);
    REQUIRE(p != NULL);
    fill_pattern(p, 16, 1);

    CHECK_EQ(_num_allocated_blocks(), 1);
    CHECK_EQ(_num_allocated_bytes(), 16);
    CHECK_EQ(_num_meta_data_bytes(), META);
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_free_bytes(), 0);

    char *q = (char *) smalloc(24);
    REQUIRE(q != NULL);
    CHECK_EQ(_num_allocated_blocks(), 2);
    CHECK_EQ(_num_allocated_bytes(), 16 + 24);
    CHECK_EQ(_num_meta_data_bytes(), 2 * META);
    CHECK(pattern_ok(p, 16, 1)); // second allocation didn't clobber the first
}

static void test_free_stats_and_double_free() {
    void *p = smalloc(32);
    REQUIRE(p != NULL);

    sfree(p);
    CHECK_EQ(_num_free_blocks(), 1);
    CHECK_EQ(_num_free_bytes(), 32);        // excludes metadata
    CHECK_EQ(_num_allocated_blocks(), 1);   // block still exists
    CHECK_EQ(_num_allocated_bytes(), 32);

    sfree(p);        // double free - must be a no-op
    sfree(NULL);     // NULL free  - must be a no-op
    CHECK_EQ(_num_free_blocks(), 1);
    CHECK_EQ(_num_free_bytes(), 32);
    CHECK_EQ(_num_allocated_blocks(), 1);
}

static void test_exact_reuse() {
    void *p = smalloc(64);
    REQUIRE(p != NULL);
    sfree(p);
    void *q = smalloc(64);
    REQUIRE(q != NULL);
    CHECK(q == p);

    CHECK_EQ(_num_allocated_blocks(), 1); // no NEW block was created
    CHECK_EQ(_num_free_blocks(), 0);
}

// spec note 12: choosing a 1000-byte block for a 10-byte request marks all
// 1000 bytes as used, and note 1: the size field never changes.
static void test_whole_block_marked_used() {
    void *p = smalloc(1000);
    REQUIRE(p != NULL);
    sfree(p);
    CHECK_EQ(_num_free_bytes(), 1000);

    void *q = smalloc(10);
    REQUIRE(q != NULL);
    CHECK(q == p);
    CHECK_EQ(_num_free_bytes(), 0);          // ALL 1000 bytes marked used
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_allocated_bytes(), 1000);  // size field unchanged

    // size field must still be 1000: an in-place srealloc up to 1000 succeeds
    void *r = srealloc(q, 1000);
    CHECK(r == q);
    CHECK_EQ(_num_allocated_blocks(), 1);
    CHECK_EQ(_num_allocated_bytes(), 1000);
}

// two free blocks fit; the LOWER-ADDRESS one must win, even if it wastes more
static void test_first_fit_lowest_address() {
    void *a = smalloc(1100);
    void *b = smalloc(1200);
    REQUIRE(a != NULL && b != NULL);
    sfree(a);
    sfree(b);
    CHECK_EQ(_num_free_blocks(), 2);
    CHECK_EQ(_num_free_bytes(), 2300);

    void *q = smalloc(500); // both fit; must take `a` (lower address)
    REQUIRE(q != NULL);
    CHECK(q == a);
    CHECK_EQ(_num_free_bytes(), 1200); // only b's bytes remain free

    void *r = smalloc(1200);
    CHECK(r == b);
}

// First-fit is by address, not by "least waste": a lower 800-byte block must
// beat a higher 700-byte block for a 650-byte request.
static void test_first_fit_fragmented_list() {
    char *a = (char *) smalloc(400);
    char *b = (char *) smalloc(800);
    char *c = (char *) smalloc(200);
    char *d = (char *) smalloc(700);
    char *e = (char *) smalloc(600);
    REQUIRE(a && b && c && d && e);

    fill_pattern(c, 200, 11);
    fill_pattern(e, 600, 12);

    sfree(d);
    sfree(b);
    sfree(a);
    CHECK_EQ(_num_free_blocks(), 3);
    CHECK_EQ(_num_free_bytes(), 400 + 800 + 700);

    void *q = smalloc(650);
    REQUIRE(q != NULL);
    CHECK(q == b);
    CHECK_EQ(_num_free_blocks(), 2);
    CHECK_EQ(_num_free_bytes(), 400 + 700);

    void *r = smalloc(650);
    REQUIRE(r != NULL);
    CHECK(r == d);
    CHECK_EQ(_num_free_blocks(), 1);
    CHECK_EQ(_num_free_bytes(), 400);
    CHECK(pattern_ok(c, 200, 11));
    CHECK(pattern_ok(e, 600, 12));
}

static void test_scalloc_zeroing() {
    // fresh block
    unsigned char *f = (unsigned char *) scalloc(10, 30); // 300 bytes
    REQUIRE(f != NULL);
    for (int i = 0; i < 300; i++) CHECK(f[i] == 0);
    fill_pattern(f, 300, 9);
    sfree(f);

    // dirty freed block: same 300 bytes come back, must be zeroed again
    unsigned char *q = (unsigned char *) scalloc(30, 10);
    REQUIRE(q != NULL);
    CHECK((void *) q == (void *) f);
    bool all_zero = true;
    for (int i = 0; i < 300; i++) all_zero = all_zero && (q[i] == 0);
    CHECK(all_zero);
}

static void test_scalloc_exact_max_boundary() {
    unsigned char *p = (unsigned char *) scalloc(2, MAX_ALLOC / 2);
    REQUIRE(p != NULL);

    CHECK(p[0] == 0);
    CHECK(p[MAX_ALLOC / 2] == 0);
    CHECK(p[MAX_ALLOC - 1] == 0);
    p[0] = 'a';
    p[MAX_ALLOC - 1] = 'z';
    CHECK(p[0] == 'a');
    CHECK(p[MAX_ALLOC - 1] == 'z');

    CHECK_EQ(_num_allocated_blocks(), 1);
    CHECK_EQ(_num_allocated_bytes(), MAX_ALLOC);
    sfree(p);
    CHECK_EQ(_num_free_blocks(), 1);
    CHECK_EQ(_num_free_bytes(), MAX_ALLOC);
}

static void test_srealloc_shrink_and_equal() {
    char *p = (char *) smalloc(1400);
    REQUIRE(p != NULL);
    fill_pattern(p, 1400, 21);

    char *q = (char *) srealloc(p, 700);   // smaller -> same block
    CHECK(q == p);
    CHECK(pattern_ok(q, 1400, 21));        // nothing was moved or clobbered

    char *r = (char *) srealloc(p, 1400);  // equal -> same block
    CHECK(r == p);

    CHECK_EQ(_num_allocated_blocks(), 1);
    CHECK_EQ(_num_allocated_bytes(), 1400);
    CHECK_EQ(_num_free_blocks(), 0);
}

static void test_srealloc_grow_copies_and_frees() {
    char *p = (char *) smalloc(1500);
    REQUIRE(p != NULL);
    fill_pattern(p, 1500, 33);

    char *q = (char *) srealloc(p, 1600); // no free block exists -> new block
    REQUIRE(q != NULL);
    CHECK(q != p);
    CHECK(pattern_ok(q, 1500, 33)); // first 1500 bytes preserved

    CHECK_EQ(_num_free_blocks(), 1); // old block was freed
    CHECK_EQ(_num_free_bytes(), 1500);
    CHECK_EQ(_num_allocated_blocks(), 2);
    CHECK_EQ(_num_allocated_bytes(), 1500 + 1600);
}

static void test_srealloc_move_frees_old_and_keeps_neighbors() {
    char *p = (char *) smalloc(257);
    char *neighbor = (char *) smalloc(257);
    REQUIRE(p && neighbor);
    fill_pattern(p, 257, 91);
    fill_pattern(neighbor, 257, 92);

    char *q = (char *) srealloc(p, 4096);
    REQUIRE(q != NULL);
    CHECK(q != p);
    CHECK(pattern_ok(q, 257, 91));
    CHECK(pattern_ok(neighbor, 257, 92));
    CHECK_EQ(_num_free_blocks(), 1);
    CHECK_EQ(_num_free_bytes(), 257);

    char *old_slot = (char *) smalloc(128);
    REQUIRE(old_slot != NULL);
    CHECK(old_slot == p);
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_allocated_blocks(), 3);
    CHECK_EQ(_num_allocated_bytes(), 257 + 257 + 4096);
}

static void test_srealloc_reuses_free_block() {
    char *ps = (char *) smalloc(1700);
    char *pb = (char *) smalloc(1800);
    REQUIRE(ps != NULL && pb != NULL);
    fill_pattern(ps, 1700, 41);
    sfree(pb);

    char *q = (char *) srealloc(ps, 1750); // pb's block (1800) fits
    REQUIRE(q != NULL);
    CHECK(q == pb);
    CHECK(pattern_ok(q, 1700, 41));
    CHECK_EQ(_num_free_blocks(), 1);  // ps's block is now the free one
    CHECK_EQ(_num_free_bytes(), 1700);
    CHECK_EQ(_num_allocated_blocks(), 2); // no new block was created
}

static void test_srealloc_failure_keeps_block() {
    char *p = (char *) smalloc(1900);
    REQUIRE(p != NULL);
    fill_pattern(p, 1900, 55);

    CHECK(srealloc(p, 0) == NULL);
    CHECK(srealloc(p, MAX_ALLOC + 1) == NULL);

    CHECK_EQ(_num_free_blocks(), 0); // p must still be allocated
    CHECK_EQ(_num_free_bytes(), 0);
    CHECK(pattern_ok(p, 1900, 55));  // content untouched
}

static void test_invalid_calls_after_allocs_preserve_state() {
    char *p = (char *) smalloc(512);
    char *q = (char *) smalloc(768);
    REQUIRE(p && q);
    fill_pattern(p, 512, 101);
    fill_pattern(q, 768, 102);

    Stats before = Stats::now();
    CHECK(smalloc(0) == NULL);
    CHECK(smalloc(MAX_ALLOC + 1) == NULL);
    CHECK(scalloc(0, 10) == NULL);
    CHECK(scalloc(10, 0) == NULL);
    CHECK(scalloc(MAX_ALLOC / 2 + 1, 2) == NULL);
    CHECK(srealloc(p, 0) == NULL);
    CHECK(srealloc(q, MAX_ALLOC + 1) == NULL);

    Stats after = Stats::now();
    CHECK_EQ(after.free_blocks, before.free_blocks);
    CHECK_EQ(after.free_bytes, before.free_bytes);
    CHECK_EQ(after.alloc_blocks, before.alloc_blocks);
    CHECK_EQ(after.alloc_bytes, before.alloc_bytes);
    CHECK_EQ(after.meta_bytes, before.meta_bytes);
    CHECK(pattern_ok(p, 512, 101));
    CHECK(pattern_ok(q, 768, 102));
}

static void test_srealloc_null_oldp() {
    char *p = (char *) srealloc(NULL, 1950);
    REQUIRE(p != NULL);
    fill_pattern(p, 1950, 60);
    CHECK(pattern_ok(p, 1950, 60));
    CHECK_EQ(_num_allocated_blocks(), 1);
    CHECK_EQ(_num_allocated_bytes(), 1950);
}

static void test_metadata_consistency() {
    CHECK(_size_meta_data() > 0);
    void *a = smalloc(100);
    void *b = smalloc(200);
    REQUIRE(a != NULL && b != NULL);
    sfree(a);

    CHECK_EQ(_num_meta_data_bytes(), _num_allocated_blocks() * _size_meta_data());
    // free blocks are a subset of allocated blocks
    CHECK(_num_free_blocks() <= _num_allocated_blocks());
    CHECK(_num_free_bytes() <= _num_allocated_bytes());
}

static void test_data_integrity_many_blocks() {
    const int N = 8;
    char *ptrs[N];
    size_t sizes[N];
    for (int i = 0; i < N; i++) {
        sizes[i] = 2000 + i * 13;
        ptrs[i] = (char *) smalloc(sizes[i]);
        REQUIRE(ptrs[i] != NULL);
        fill_pattern(ptrs[i], sizes[i], (unsigned char) (i + 70));
    }
    for (int i = 0; i < N; i++) {
        CHECK(pattern_ok(ptrs[i], sizes[i], (unsigned char) (i + 70)));
    }

    // free every other block, allocate new ones, survivors must be intact
    for (int i = 0; i < N; i += 2) sfree(ptrs[i]);
    void *extra = smalloc(3000); // bigger than any hole -> fresh block
    REQUIRE(extra != NULL);
    for (int i = 1; i < N; i += 2) {
        CHECK(pattern_ok(ptrs[i], sizes[i], (unsigned char) (i + 70)));
    }
}

static void test_max_boundary() {
    char *p = (char *) smalloc(MAX_ALLOC);
    REQUIRE(p != NULL);
    p[0] = 'a';
    p[MAX_ALLOC - 1] = 'z';
    CHECK(p[0] == 'a' && p[MAX_ALLOC - 1] == 'z');
    sfree(p);
    CHECK_EQ(_num_free_bytes(), MAX_ALLOC);
    CHECK_EQ(_num_allocated_bytes(), MAX_ALLOC);
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
    std::cout << "malloc_2 comprehensive tests (each test in its own process):"
              << std::endl;
    META = _size_meta_data();

    const TestCase tests[] = {
        {"Test 1: invalid-argument rejection",                        test_rejects},
        {"Test 2: smalloc statistics",                                test_alloc_stats},
        {"Test 3: sfree stats, NULL free, double free",               test_free_stats_and_double_free},
        {"Test 4: freed block is reused (same pointer)",              test_exact_reuse},
        {"Test 5: big block reused for small request (whole block)",  test_whole_block_marked_used},
        {"Test 6: ascending-order search (first fit by address)",     test_first_fit_lowest_address},
        {"Test 7: first-fit through fragmented free list",            test_first_fit_fragmented_list},
        {"Test 8: scalloc zeroes fresh and DIRTY reused blocks",      test_scalloc_zeroing},
        {"Test 9: scalloc exact 10^8 boundary",                       test_scalloc_exact_max_boundary},
        {"Test 10: srealloc shrink / equal size stays in place",      test_srealloc_shrink_and_equal},
        {"Test 11: srealloc grow copies content and frees old block", test_srealloc_grow_copies_and_frees},
        {"Test 12: srealloc move frees old slot and keeps neighbors", test_srealloc_move_frees_old_and_keeps_neighbors},
        {"Test 13: srealloc grow prefers an existing free block",     test_srealloc_reuses_free_block},
        {"Test 14: failed srealloc must NOT free oldp",               test_srealloc_failure_keeps_block},
        {"Test 15: invalid calls after allocs preserve state",        test_invalid_calls_after_allocs_preserve_state},
        {"Test 16: srealloc(NULL, n) behaves like smalloc",           test_srealloc_null_oldp},
        {"Test 17: metadata bookkeeping is self-consistent",          test_metadata_consistency},
        {"Test 18: pattern integrity across many live blocks",        test_data_integrity_many_blocks},
        {"Test 19: exact 10^8 boundary",                              test_max_boundary},
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
