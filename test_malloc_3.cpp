// Comprehensive tests for Part 3 - buddy allocator (malloc_3.cpp).
//
// Spec highlights being tested (HW4-wet, part 3):
//   - lazy init: first smalloc creates 32 free blocks of 128KB; sbrk is never
//     used again afterwards (checked via sbrk(0) inside the tests)
//   - block sizes are 128 * 2^order (order 0..10), sizes INCLUDE the metadata
//   - tightest fit: smallest free order that fits; among equal orders the
//     lowest address wins; blocks are split iteratively while "large enough"
//   - sfree merges free buddies iteratively (never above order 10)
//   - allocations with size + meta > 128KB go through mmap/munmap
//   - srealloc: (a) reuse current block, (b) grow by merging buddies,
//     (c) otherwise relocate (freeing + merging the old block);
//     mmap'ed blocks are never reused (unless equal size)
//   - stats 5-10 keep the part-2 definitions: byte counts EXCLUDE metadata,
//     mmap'ed blocks count as allocated (never as free)
//
// Isolation: each test runs in its own fork()ed child process, so every test
// starts with a completely uninitialized allocator - no state leaks between
// tests.  A crashing test is reported (signal) and the suite keeps going.
// Tests still call VERIFY_CLEAN() at the end to prove that freeing merges
// everything back to 32 free 128KB blocks.

#include <iostream>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "os_malloc.h"

#define MAX_ALLOC   100000000ULL          // 10^8
#define BLOCK_SIZE  (128 * 1024)          // order-10 block, 128KB
#define NUM_INIT    32
#define HEAP_TOTAL  ((size_t) NUM_INIT * BLOCK_SIZE) // 4MB

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

static size_t META;

static size_t clean_bytes() { return HEAP_TOTAL - NUM_INIT * META; }

// heap must be back to 32 free 128KB blocks after every scenario
static void verify_clean(int line) {
    bool ok = _num_allocated_blocks() == NUM_INIT &&
              _num_free_blocks() == NUM_INIT &&
              _num_allocated_bytes() == clean_bytes() &&
              _num_free_bytes() == clean_bytes() &&
              _num_meta_data_bytes() == NUM_INIT * META;
    if (!ok) {
        ++g_failed;
        std::cout << "\n    CLEAN-STATE CHECK FAILED (called from line " << line
                  << "): blocks=" << _num_allocated_blocks()
                  << " free_blocks=" << _num_free_blocks()
                  << " alloc_bytes=" << _num_allocated_bytes()
                  << " free_bytes=" << _num_free_bytes()
                  << " meta_bytes=" << _num_meta_data_bytes()
                  << " (expected 32 / 32 / " << clean_bytes() << " / "
                  << clean_bytes() << " / " << NUM_INIT * META << ")"
                  << std::flush;
    }
}
#define VERIFY_CLEAN() verify_clean(__LINE__)

static uintptr_t block_base(void *user_ptr) {
    return (uintptr_t) user_ptr - META;
}

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
// every test starts with an UNINITIALIZED allocator (fresh child process)

// before the first real allocation nothing was sbrk'ed - all stats must be 0,
// and rejected requests must not trigger the lazy init
static void test_pre_init() {
    void *brk_before = sbrk(0);

    CHECK(smalloc(0) == NULL);
    CHECK(smalloc(MAX_ALLOC + 1) == NULL);
    CHECK(scalloc(0, 4) == NULL);
    CHECK(scalloc(4, 0) == NULL);
    CHECK(scalloc(1, MAX_ALLOC + 1) == NULL);
    CHECK(scalloc(MAX_ALLOC + 1, 1) == NULL);
    CHECK(scalloc(MAX_ALLOC, 2) == NULL);
    CHECK(scalloc((size_t)1 << (sizeof(size_t) * 8 - 1), 2) == NULL); // overflow
    CHECK(srealloc(NULL, 0) == NULL);
    CHECK(sbrk(0) == brk_before);

    CHECK_EQ(_num_allocated_blocks(), 0);
    CHECK_EQ(_num_allocated_bytes(), 0);
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_free_bytes(), 0);
    CHECK_EQ(_num_meta_data_bytes(), 0);
    CHECK(META <= 64); // hard requirement from the pdf
}

// first smalloc(1): one 128KB block is split 10 times ->
// 31 untouched + 10 free buddies (orders 0..9) + 1 used order-0 block
static void test_init_and_split_accounting() {
    char *p = (char *) smalloc(1);
    REQUIRE(p != NULL);
    *p = 'x';

    CHECK_EQ(_num_allocated_blocks(), 42); // 31 + 10 + 1
    CHECK_EQ(_num_free_blocks(), 41);
    CHECK_EQ(_num_meta_data_bytes(), 42 * META);
    CHECK_EQ(_num_allocated_bytes(), HEAP_TOTAL - 42 * META);
    // everything is free except the used order-0 block's payload
    CHECK_EQ(_num_free_bytes(), HEAP_TOTAL - 42 * META - (128 - META));
    CHECK(((uintptr_t) p % 8) == 0);

    sfree(p); // must cascade-merge all the way back up
    VERIFY_CLEAN();
}

// tightest-fit + minimal-address + splitting force an exact placement for a
// run of small allocations: block bases at offsets 0,128,256,...,896
static void test_deterministic_layout() {
    const int N = 8;
    char *p[N];
    p[0] = (char *) smalloc(1);
    REQUIRE(p[0] != NULL);
    uintptr_t base = block_base(p[0]);

    for (int i = 1; i < N; i++) {
        p[i] = (char *) smalloc(1);
        REQUIRE(p[i] != NULL);
        CHECK_EQ((uintptr_t) p[i], base + 128 * i + META);
    }

    // free in a shuffled order - buddy merging must still fully reassemble
    int order[N] = {5, 0, 3, 7, 2, 6, 1, 4};
    for (int i = 0; i < N; i++) sfree(p[order[i]]);
    VERIFY_CLEAN();
}

// max payload of order k is 128*2^k - META; one byte more must bump the order
static void test_order_boundaries() {
    size_t s0max = 128 - META;

    // two order-0 allocations sit exactly 128 bytes apart
    char *a = (char *) smalloc(s0max);
    char *b = (char *) smalloc(s0max);
    REQUIRE(a != NULL && b != NULL);
    CHECK_EQ((uintptr_t) b - (uintptr_t) a, 128);
    sfree(a);
    sfree(b);
    VERIFY_CLEAN();

    // s0max+1 needs an order-1 block: the next small allocation cannot use
    // offset 128 (it's inside the order-1 block) and must go to offset 256
    char *c = (char *) smalloc(s0max + 1);
    REQUIRE(c != NULL);
    uintptr_t base = block_base(c);
    char *d = (char *) smalloc(1);
    REQUIRE(d != NULL);
    CHECK_EQ((uintptr_t) d, base + 256 + META);
    sfree(c);
    sfree(d);
    VERIFY_CLEAN();
}

static void test_large_first_allocation_initializes_once() {
    size_t large_size = 256 * 1024;

    char *p = (char *) smalloc(large_size);
    REQUIRE(p != NULL);
    void *brk_after_first = sbrk(0);
    fill_pattern(p, 4096, 44);

    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 1);
    CHECK_EQ(_num_free_blocks(), NUM_INIT);
    CHECK_EQ(_num_free_bytes(), clean_bytes());
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + large_size);

    char *q = (char *) smalloc(large_size + 4096);
    REQUIRE(q != NULL);
    CHECK(sbrk(0) == brk_after_first);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 2);
    CHECK(pattern_ok(p, 4096, 44));

    sfree(p);
    sfree(q);
    VERIFY_CLEAN();
    CHECK(sbrk(0) == brk_after_first);
}

// Tightest fit wins before address order across different sizes: the higher
// order-0 buddy must beat a lower-address order-10 block.
static void test_tightest_fit_beats_lower_large_block() {
    char *large = (char *) smalloc(BLOCK_SIZE - META);
    char *small = (char *) smalloc(1);
    REQUIRE(large && small);

    uintptr_t small_base = block_base(small);
    sfree(large);

    char *tiny = (char *) smalloc(1);
    REQUIRE(tiny != NULL);
    CHECK_EQ((uintptr_t) tiny, small_base + 128 + META);

    sfree(small);
    sfree(tiny);
    VERIFY_CLEAN();
}

// a request for a full 128KB block must not split anything
static void test_exact_max_order_no_split() {
    void *p = smalloc(BLOCK_SIZE - META);
    REQUIRE(p != NULL);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT);
    CHECK_EQ(_num_free_blocks(), NUM_INIT - 1);
    CHECK_EQ(_num_free_bytes(), (NUM_INIT - 1) * (size_t)(BLOCK_SIZE - META));
    CHECK_EQ(_num_allocated_bytes(), clean_bytes());
    sfree(p);
    VERIFY_CLEAN();
}

// merging must stop at a used buddy and resume once it is freed
static void test_merge_requires_free_buddy() {
    char *p1 = (char *) smalloc(1); // offset 0
    char *p2 = (char *) smalloc(1); // offset 128, buddy of p1
    char *p3 = (char *) smalloc(1); // offset 256
    char *p4 = (char *) smalloc(1); // offset 384, buddy of p3
    REQUIRE(p1 && p2 && p3 && p4);
    CHECK_EQ(_num_allocated_blocks(), 43);
    CHECK_EQ(_num_free_blocks(), 39);

    sfree(p1); // buddy p2 used -> no merge
    CHECK_EQ(_num_allocated_blocks(), 43);
    CHECK_EQ(_num_free_blocks(), 40);

    sfree(p3); // buddy p4 used -> no merge
    CHECK_EQ(_num_allocated_blocks(), 43);
    CHECK_EQ(_num_free_blocks(), 41);

    sfree(p2); // merges with p1 into order 1; buddy at 256 is order 0 -> stop
    CHECK_EQ(_num_allocated_blocks(), 42);
    CHECK_EQ(_num_free_blocks(), 41);

    sfree(p4); // full cascade back to a single 128KB block
    VERIFY_CLEAN();
}

static void test_partial_coalescing_reused_by_tight_fit() {
    const int N = 16;
    char *p[N];
    for (int i = 0; i < N; i++) {
        p[i] = (char *) smalloc(1);
        REQUIRE(p[i] != NULL);
        fill_pattern(p[i], 1, (unsigned char) (30 + i));
    }
    uintptr_t base = block_base(p[0]);

    int order[8] = {3, 0, 7, 2, 5, 1, 6, 4};
    for (int i = 0; i < 8; i++) sfree(p[order[i]]);

    char *big = (char *) smalloc(1024 - META);
    REQUIRE(big != NULL);
    CHECK_EQ((uintptr_t) big, base + META);

    for (int i = 8; i < N; i++) {
        CHECK(pattern_ok(p[i], 1, (unsigned char) (30 + i)));
    }

    sfree(big);
    for (int i = 8; i < N; i++) sfree(p[i]);
    VERIFY_CLEAN();
}

static void test_free_edge_cases() {
    sfree(NULL); // must not crash or init anything
    CHECK_EQ(_num_allocated_blocks(), 0);

    char *p = (char *) smalloc(1);
    char *q = (char *) smalloc(1);
    REQUIRE(p && q);
    sfree(p);
    sfree(p); // double free, block currently free -> no-op
    sfree(q);
    sfree(q); // double free after merging -> still a no-op
    sfree(p);
    VERIFY_CLEAN();
}

static void test_scalloc_zeroing() {
    // dirty the region first
    unsigned char *p = (unsigned char *) smalloc(100);
    REQUIRE(p != NULL);
    fill_pattern(p, 100, 0xA0);
    sfree(p);
    VERIFY_CLEAN();

    // same block comes back through scalloc - must be zeroed
    unsigned char *q = (unsigned char *) scalloc(25, 4); // 100 bytes
    REQUIRE(q != NULL);
    CHECK((void *) q == (void *) p);
    bool all_zero = true;
    for (int i = 0; i < 100; i++) all_zero = all_zero && (q[i] == 0);
    CHECK(all_zero);
    sfree(q);
    VERIFY_CLEAN();
}

static void test_scalloc_heap_threshold_boundary() {
    size_t heap_max = BLOCK_SIZE - META;

    unsigned char *p = (unsigned char *) scalloc(heap_max, 1);
    REQUIRE(p != NULL);
    CHECK(p[0] == 0);
    CHECK(p[heap_max - 1] == 0);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT);
    CHECK_EQ(_num_free_blocks(), NUM_INIT - 1);
    p[heap_max - 1] = 'h';
    CHECK(p[heap_max - 1] == 'h');
    sfree(p);
    VERIFY_CLEAN();

    unsigned char *q = (unsigned char *) scalloc(heap_max + 1, 1);
    REQUIRE(q != NULL);
    CHECK(q[0] == 0);
    CHECK(q[heap_max] == 0);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 1);
    CHECK_EQ(_num_free_blocks(), NUM_INIT);
    sfree(q);
    VERIFY_CLEAN();
}

// size + meta == 128KB stays on the heap; one byte more goes to mmap
static void test_mmap_threshold() {
    size_t heap_max = BLOCK_SIZE - META;

    void *heap_p = smalloc(heap_max);
    REQUIRE(heap_p != NULL);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT); // no mmap happened
    sfree(heap_p);
    VERIFY_CLEAN();

    char *m = (char *) smalloc(heap_max + 1); // size + meta > 128KB -> mmap
    REQUIRE(m != NULL);
    m[0] = 'a';
    m[heap_max] = 'z';
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 1);
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + heap_max + 1);
    CHECK_EQ(_num_meta_data_bytes(), (NUM_INIT + 1) * META);
    // mmap'ed blocks are never "free"; heap itself is untouched
    CHECK_EQ(_num_free_blocks(), NUM_INIT);
    CHECK_EQ(_num_free_bytes(), clean_bytes());

    sfree(m); // munmap - block disappears from the stats entirely
    VERIFY_CLEAN();
}

static void test_mmap_multiple_and_list_removal() {
    size_t sa = 200 * 1024, sb = 300 * 1024, sc = 400 * 1024;
    char *a = (char *) smalloc(sa);
    char *b = (char *) smalloc(sb);
    char *c = (char *) smalloc(sc);
    REQUIRE(a && b && c);
    fill_pattern(a, 256, 1);
    fill_pattern(b, 256, 2);
    fill_pattern(c, 256, 3);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 3);
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + sa + sb + sc);

    sfree(b); // middle of the mmap list
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 2);
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + sa + sc);
    CHECK(pattern_ok(a, 256, 1));
    CHECK(pattern_ok(c, 256, 3));

    sfree(a);
    sfree(c);
    VERIFY_CLEAN();

    // scalloc large enough to be mmap'ed must be zeroed too
    unsigned char *z = (unsigned char *) scalloc(1024, 200); // 200KB
    REQUIRE(z != NULL);
    bool all_zero = true;
    for (int i = 0; i < 200 * 1024; i += 977) all_zero = all_zero && (z[i] == 0);
    CHECK(all_zero);
    sfree(z);
    VERIFY_CLEAN();
}

static void test_mmap_max_boundary() {
    char *p = (char *) smalloc(MAX_ALLOC);
    REQUIRE(p != NULL);
    p[0] = 'a';
    p[MAX_ALLOC - 1] = 'z';
    CHECK(p[0] == 'a' && p[MAX_ALLOC - 1] == 'z');
    CHECK(smalloc(MAX_ALLOC + 1) == NULL);
    sfree(p);
    VERIFY_CLEAN();
}

// priority (a): the current block already fits -> same pointer, no motion
static void test_srealloc_reuse_current() {
    size_t order1_max = 256 - META;
    char *p = (char *) smalloc(200); // 200 + meta > 128 -> order-1 block
    REQUIRE(p != NULL);
    fill_pattern(p, 200, 77);

    CHECK(srealloc(p, 50) == p);          // shrink
    CHECK(srealloc(p, 200) == p);         // same size
    CHECK(srealloc(p, order1_max) == p);  // grow within the same block
    CHECK(pattern_ok(p, 200, 77));

    sfree(p);
    VERIFY_CLEAN();
}

// priority (b): merge with a free buddy instead of relocating
static void test_srealloc_merge_with_buddy() {
    size_t order1_max = 256 - META;
    size_t s0_payload = 128 - META;
    size_t fill_n = s0_payload < 60 ? s0_payload : 60;

    // case A: we are the LEFT buddy - pointer must stay the same
    char *p1 = (char *) smalloc(1); // offset 0
    char *p2 = (char *) smalloc(1); // offset 128
    REQUIRE(p1 && p2);
    fill_pattern(p1, fill_n, 81);
    sfree(p2);
    char *q = (char *) srealloc(p1, order1_max);
    REQUIRE(q != NULL);
    CHECK(q == p1);
    CHECK(pattern_ok(q, fill_n, 81));
    CHECK_EQ(_num_allocated_blocks(), 41); // two order-0 became one order-1
    CHECK_EQ(_num_free_blocks(), 40);
    sfree(q);
    VERIFY_CLEAN();

    // case B: we are the RIGHT buddy - data must move to the left buddy
    p1 = (char *) smalloc(1); // offset 0
    p2 = (char *) smalloc(1); // offset 128
    REQUIRE(p1 && p2);
    uintptr_t base = block_base(p1);
    fill_pattern(p2, fill_n, 82);
    sfree(p1);
    q = (char *) srealloc(p2, order1_max);
    REQUIRE(q != NULL);
    CHECK_EQ((uintptr_t) q, base + META); // moved to the merged block's start
    CHECK(pattern_ok(q, fill_n, 82));
    sfree(q);
    VERIFY_CLEAN();
}

static void test_srealloc_multilevel_merge() {
    size_t order2_max = 512 - META;
    size_t s0_payload = 128 - META;
    size_t fill_n = s0_payload < 60 ? s0_payload : 60;

    char *p[5];
    for (int i = 0; i < 5; i++) {
        p[i] = (char *) smalloc(1);
        REQUIRE(p[i] != NULL);
    }

    fill_pattern(p[0], fill_n, 87);
    sfree(p[2]);
    sfree(p[3]); // p[2] and p[3] merge into an order-1 block
    sfree(p[1]); // p[0]'s direct buddy is now free too

    char *q = (char *) srealloc(p[0], order2_max);
    REQUIRE(q != NULL);
    CHECK(q == p[0]);
    CHECK(pattern_ok(q, fill_n, 87));

    sfree(q);
    sfree(p[4]);
    VERIFY_CLEAN();
}

static void test_srealloc_multilevel_merge_from_right() {
    size_t order2_max = 512 - META;
    size_t s0_payload = 128 - META;
    size_t fill_n = s0_payload < 60 ? s0_payload : 60;

    char *p[5];
    for (int i = 0; i < 5; i++) {
        p[i] = (char *) smalloc(1);
        REQUIRE(p[i] != NULL);
    }
    uintptr_t base = block_base(p[0]);

    fill_pattern(p[3], fill_n, 89);
    sfree(p[0]);
    sfree(p[1]); // p[0] and p[1] merge into an order-1 block
    sfree(p[2]); // p[3]'s direct buddy is now free too

    char *q = (char *) srealloc(p[3], order2_max);
    REQUIRE(q != NULL);
    CHECK_EQ((uintptr_t) q, base + META);
    CHECK(pattern_ok(q, fill_n, 89));

    sfree(q);
    sfree(p[4]);
    VERIFY_CLEAN();
}

// priority (c): buddy is used -> relocate, and the old block must be freed
static void test_srealloc_relocate() {
    size_t order1_max = 256 - META;
    size_t s0_payload = 128 - META;
    size_t fill_n = s0_payload < 60 ? s0_payload : 60;

    char *p1 = (char *) smalloc(1); // offset 0
    char *p2 = (char *) smalloc(1); // offset 128 - stays used, blocks merging
    REQUIRE(p1 && p2);
    uintptr_t base = block_base(p1);
    fill_pattern(p1, fill_n, 83);

    char *q = (char *) srealloc(p1, order1_max);
    REQUIRE(q != NULL);
    CHECK_EQ((uintptr_t) q, base + 256 + META); // tightest free order-1 block
    CHECK(pattern_ok(q, fill_n, 83));
    CHECK_EQ(_num_allocated_blocks(), 42);
    CHECK_EQ(_num_free_blocks(), 40); // old p1 block free, p2 + q used

    // The old block must really be returned to the free lists after a move.
    char *r = (char *) smalloc(1);
    REQUIRE(r != NULL);
    CHECK(r == p1);

    sfree(p2);
    sfree(q);
    sfree(r);
    VERIFY_CLEAN();
}

static void test_srealloc_null_zero_max() {
    // 128 bytes of payload plus any valid metadata size always requires an
    // order-1 block.  Using 100 here made the expected free-block count depend
    // on whether the metadata happened to be 24 or 32 bytes.
    const size_t payload_size = 128;
    char *p = (char *) srealloc(NULL, payload_size); // acts like smalloc
    REQUIRE(p != NULL);
    fill_pattern(p, payload_size, 84);

    const size_t allocated_blocks = _num_allocated_blocks();
    const size_t allocated_bytes = _num_allocated_bytes();
    const size_t free_blocks = _num_free_blocks();
    const size_t free_bytes = _num_free_bytes();

    CHECK(srealloc(p, 0) == NULL);
    CHECK(srealloc(p, MAX_ALLOC + 1) == NULL);
    CHECK(srealloc(p, (size_t) -1) == NULL);
    CHECK(pattern_ok(p, payload_size, 84)); // failures preserve oldp and its data
    CHECK_EQ(_num_allocated_blocks(), allocated_blocks);
    CHECK_EQ(_num_allocated_bytes(), allocated_bytes);
    CHECK_EQ(_num_free_blocks(), free_blocks);
    CHECK_EQ(_num_free_bytes(), free_bytes);
    CHECK_EQ(_num_free_blocks(), 40); // p (order-1) still used

    sfree(p);
    VERIFY_CLEAN();
}

// Exercise both sides of every buddy-order payload boundary.  The largest
// payload that fits order k must stay in place; one byte more must grow to
// order k+1.  At order 10, that extra byte must cross from sbrk to mmap.
static void test_srealloc_all_order_boundaries() {
    for (size_t order = 0; order <= 10; order++) {
        const size_t block_size = (size_t) 128 << order;
        REQUIRE(block_size > META);

        const size_t max_payload = block_size - META;
        const size_t fill_n = max_payload < 64 ? max_payload : 64;
        const unsigned char seed = (unsigned char) (110 + order);

        char *p = (char *) smalloc(max_payload);
        REQUIRE(p != NULL);
        fill_pattern(p, fill_n, seed);

        // Exact boundary and shrinking by one both reuse the current block.
        CHECK(srealloc(p, max_payload) == p);
        if (max_payload > 1) {
            CHECK(srealloc(p, max_payload - 1) == p);
        }
        CHECK(pattern_ok(p, fill_n, seed));

        char *q = (char *) srealloc(p, max_payload + 1);
        REQUIRE(q != NULL);
        if (order < 10) {
            // The allocation starts as the left buddy and can grow in place.
            CHECK(q == p);
        } else {
            // No buddy order exists above 10, so growth relocates to mmap.
            CHECK(q != p);
        }
        CHECK(pattern_ok(q, fill_n, seed));

        sfree(q);
        VERIFY_CLEAN();
    }
}

static void test_srealloc_mmap() {
    size_t s1 = 200 * 1024, s2 = 300 * 1024, s3 = 160 * 1024;

    char *p = (char *) smalloc(s1);
    REQUIRE(p != NULL);
    fill_pattern(p, 4096, 85);
    p[s1 - 1] = 'e';

    char *q = (char *) srealloc(p, s2); // grow
    REQUIRE(q != NULL);
    CHECK(q != p);
    CHECK(pattern_ok(q, 4096, 85));
    CHECK(q[s1 - 1] == 'e');
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 1); // old one was unmapped
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + s2);

    char *r = (char *) srealloc(q, s3); // shrink - still must relocate
    REQUIRE(r != NULL);
    CHECK(r != q);
    CHECK(pattern_ok(r, 4096, 85));
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + s3);

    sfree(r);
    VERIFY_CLEAN();
}

static void test_srealloc_mmap_same_size_reuses() {
    size_t sz = 220 * 1024;

    char *p = (char *) smalloc(sz);
    REQUIRE(p != NULL);
    fill_pattern(p, 4096, 86);
    p[sz - 1] = 'q';

    char *q = (char *) srealloc(p, sz);
    REQUIRE(q != NULL);
    CHECK(q == p);
    CHECK(pattern_ok(q, 4096, 86));
    CHECK(q[sz - 1] == 'q');
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT + 1);
    CHECK_EQ(_num_allocated_bytes(), clean_bytes() + sz);

    sfree(q);
    VERIFY_CLEAN();
}

static void test_srealloc_mmap_invalid_preserves_block() {
    size_t sz = 240 * 1024;

    char *p = (char *) smalloc(sz);
    REQUIRE(p != NULL);
    fill_pattern(p, 4096, 88);
    p[sz - 1] = 'x';

    size_t blocks_before = _num_allocated_blocks();
    size_t bytes_before = _num_allocated_bytes();
    size_t free_blocks_before = _num_free_blocks();
    size_t free_bytes_before = _num_free_bytes();
    CHECK(srealloc(p, 0) == NULL);
    CHECK(srealloc(p, MAX_ALLOC + 1) == NULL);
    CHECK_EQ(_num_allocated_blocks(), blocks_before);
    CHECK_EQ(_num_allocated_bytes(), bytes_before);
    CHECK_EQ(_num_free_blocks(), free_blocks_before);
    CHECK_EQ(_num_free_bytes(), free_bytes_before);
    CHECK(pattern_ok(p, 4096, 88));
    CHECK(p[sz - 1] == 'x');

    sfree(p);
    VERIFY_CLEAN();
}

// all 32 blocks in use -> the next heap request must fail (no more sbrk)
static void test_exhaustion() {
    void *p[NUM_INIT];
    p[0] = smalloc(BLOCK_SIZE - META); // triggers init
    REQUIRE(p[0] != NULL);
    void *brk_after_init = sbrk(0);

    for (int i = 1; i < NUM_INIT; i++) {
        p[i] = smalloc(BLOCK_SIZE - META);
        REQUIRE(p[i] != NULL);
        for (int j = 0; j < i; j++) CHECK(p[i] != p[j]);
    }
    CHECK_EQ(_num_free_blocks(), 0);
    CHECK_EQ(_num_free_bytes(), 0);
    CHECK_EQ(_num_allocated_blocks(), NUM_INIT);

    CHECK(smalloc(1) == NULL);
    CHECK(smalloc(BLOCK_SIZE - META) == NULL);
    CHECK(scalloc(1, 1) == NULL);

    for (int i = 0; i < NUM_INIT; i++) sfree(p[i]);
    VERIFY_CLEAN();

    // the buddy allocator must never sbrk after its initial region
    CHECK(sbrk(0) == brk_after_init);

    void *again = smalloc(1);
    REQUIRE(again != NULL);
    sfree(again);
    VERIFY_CLEAN();
    CHECK(sbrk(0) == brk_after_init);
}

static void test_stress_integrity() {
    const int N = 40;
    char *p[N];
    size_t sz[N];

    p[0] = (char *) smalloc(173 % 2000 + 1); // triggers init
    sz[0] = 173 % 2000 + 1;
    REQUIRE(p[0] != NULL);
    fill_pattern(p[0], sz[0], 0);
    void *brk_after_init = sbrk(0);

    for (int i = 1; i < N; i++) {
        sz[i] = (i * 173) % 2000 + 1;
        p[i] = (char *) ((i % 5 == 0) ? scalloc(sz[i], 1) : smalloc(sz[i]));
        REQUIRE(p[i] != NULL);
        fill_pattern(p[i], sz[i], (unsigned char) i);
    }
    for (int i = 0; i < N; i++) {
        CHECK(pattern_ok(p[i], sz[i], (unsigned char) i));
    }

    // free the even ones, allocate new blocks, check the odd ones survived
    for (int i = 0; i < N; i += 2) sfree(p[i]);
    char *q[N / 2];
    size_t qsz[N / 2];
    for (int i = 0; i < N / 2; i++) {
        qsz[i] = (i * 97) % 1500 + 1;
        q[i] = (char *) smalloc(qsz[i]);
        REQUIRE(q[i] != NULL);
        fill_pattern(q[i], qsz[i], (unsigned char) (i + 100));
    }
    for (int i = 1; i < N; i += 2) {
        CHECK(pattern_ok(p[i], sz[i], (unsigned char) i));
    }
    for (int i = 0; i < N / 2; i++) {
        CHECK(pattern_ok(q[i], qsz[i], (unsigned char) (i + 100)));
    }

    for (int i = 1; i < N; i += 2) sfree(p[i]);
    for (int i = 0; i < N / 2; i++) sfree(q[i]);
    VERIFY_CLEAN();
    CHECK(sbrk(0) == brk_after_init); // heap never grew past the init region
}

// ---------------------------------------------------------------------------

struct TestCase {
    const char *name;
    void (*fn)();
};

// runs one test in a forked child so it gets a pristine, uninitialized heap
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
    std::cout << "malloc_3 comprehensive tests (each test in its own process):"
              << std::endl;
    META = _size_meta_data();

    const TestCase tests[] = {
        {"Test 1: rejects before init, stats start at zero",        test_pre_init},
        {"Test 2: lazy init + split accounting",                    test_init_and_split_accounting},
        {"Test 3: deterministic placement of 8 small allocations",  test_deterministic_layout},
        {"Test 4: order-size boundaries",                           test_order_boundaries},
        {"Test 5: mmap-sized first allocation initializes once",     test_large_first_allocation_initializes_once},
        {"Test 6: tightest fit beats lower-address large block",    test_tightest_fit_beats_lower_large_block},
        {"Test 7: exact order-10 fit does not split",               test_exact_max_order_no_split},
        {"Test 8: no merge while buddy is used, cascade after",     test_merge_requires_free_buddy},
        {"Test 9: partial coalescing reused by tight fit",          test_partial_coalescing_reused_by_tight_fit},
        {"Test 10: sfree(NULL) and double free are no-ops",         test_free_edge_cases},
        {"Test 11: scalloc zeroes fresh and dirty reused memory",   test_scalloc_zeroing},
        {"Test 12: scalloc heap/mmap threshold boundary",           test_scalloc_heap_threshold_boundary},
        {"Test 13: mmap threshold boundary + stats",                test_mmap_threshold},
        {"Test 14: several mmap blocks, freeing head/middle/tail",  test_mmap_multiple_and_list_removal},
        {"Test 15: 10^8 boundary via mmap",                         test_mmap_max_boundary},
        {"Test 16: srealloc reuses current block when it fits",     test_srealloc_reuse_current},
        {"Test 17: srealloc grows by merging free buddies",         test_srealloc_merge_with_buddy},
        {"Test 18: srealloc grows through multiple buddy levels",   test_srealloc_multilevel_merge},
        {"Test 19: srealloc multilevel merge can move left",        test_srealloc_multilevel_merge_from_right},
        {"Test 20: srealloc relocates when buddy is used",          test_srealloc_relocate},
        {"Test 21: srealloc NULL / size 0 / size > 10^8",           test_srealloc_null_zero_max},
        {"Test 22: srealloc on mmap'ed blocks always relocates",    test_srealloc_mmap},
        {"Test 23: srealloc same-size mmap block is reused",        test_srealloc_mmap_same_size_reuses},
        {"Test 24: invalid mmap srealloc preserves block",          test_srealloc_mmap_invalid_preserves_block},
        {"Test 25: heap exhaustion + no extra sbrk",                test_exhaustion},
        {"Test 26: alloc/free stress with pattern verification",    test_stress_integrity},
        {"Test 27: srealloc boundaries for every buddy order",      test_srealloc_all_order_boundaries},
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
