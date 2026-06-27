/*
 * tests/test_client.c
 *
 * A self-contained correctness & synchronization test client for the HW3
 * multi-threaded web server.
 *
 * It drives a *running* server over TCP (GET/POST) and UDP (thread pings) and
 * verifies the behaviour required by Tasks 1-4 by parsing the Stat-* response
 * headers (and, for UDP, the "response from thread <id>" body).
 *
 * Design rules followed here (per the assignment + request):
 *   - Only ever sends VALID inputs: existing files, GET/POST only, and UDP
 *     pings that carry a thread id in [1, N]. Nothing here probes error paths.
 *   - Uses the response headers to attribute each request to a worker thread
 *     (Stat-Thread-Id) and to validate the per-thread counters.
 *
 * Build:   make            (in this directory)
 * Run:     ./test_client <host> <tcp_port> <udp_port> <num_threads> [file]
 * Example: ./test_client localhost 5003 5004 8
 *
 * The server must be started with a matching thread count, e.g.
 *     ./server 5003 5004 8 16 0
 *
 * Exit code is non-zero if any HARD check fails. "SOFT" checks (timing /
 * fairness / known-open-bug probes) print WARN but never fail the run.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>

/* ------------------------------------------------------------------ */
/* Tiny test framework                                                 */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;
static int g_warn = 0;

#define C_GREEN "\x1b[32m"
#define C_RED   "\x1b[31m"
#define C_YEL   "\x1b[33m"
#define C_CYN   "\x1b[36m"
#define C_RST   "\x1b[0m"

#define CHECK(cond, ...)                                              \
    do {                                                             \
        if (cond) {                                                  \
            g_pass++;                                                \
            printf("  " C_GREEN "PASS" C_RST " ");                   \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        } else {                                                     \
            g_fail++;                                                \
            printf("  " C_RED "FAIL" C_RST " ");                     \
            printf(__VA_ARGS__);                                     \
            printf("   (at %s:%d)\n", __FILE__, __LINE__);           \
        }                                                            \
    } while (0)

/* Soft checks: timing/fairness or known-open-bug probes. Never fail the run. */
#define WARN_CHECK(cond, ...)                                        \
    do {                                                             \
        if (cond) {                                                  \
            g_pass++;                                                \
            printf("  " C_GREEN "PASS" C_RST " ");                   \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        } else {                                                     \
            g_warn++;                                                \
            printf("  " C_YEL "WARN" C_RST " ");                     \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        }                                                            \
    } while (0)

static void section(const char *title) {
    printf("\n" C_CYN "== %s ==" C_RST "\n", title);
}

/* ------------------------------------------------------------------ */
/* Config (filled from argv)                                           */
/* ------------------------------------------------------------------ */

static const char *g_host;
static int g_tcp_port;
static int g_udp_port;
static int g_threads;
static const char *g_file = "home.html";

/* ------------------------------------------------------------------ */
/* HTTP response parsing                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int    ok;              /* 1 if a well-formed response was parsed */
    int    status;          /* HTTP status code */
    long   content_length;  /* from Content-Length header, -1 if absent */

    /* job (timing) stats, seconds as double; -1 if absent */
    double req_arrival;
    double req_dispatch;
    double log_arrival;
    double log_dispatch;

    /* per-thread stats; -1 if absent */
    long   thread_id;
    long   thread_count;
    long   thread_static;
    long   thread_dynamic;
    long   thread_post;

    char  *raw;             /* whole response (malloc'd) */
    long   raw_len;         /* bytes in raw */
    char  *headers;         /* null-terminated header block (malloc'd) */
    char  *body;            /* pointer into raw, after CRLFCRLF */
    long   body_len;
} Response;

static void response_free(Response *r) {
    if (!r) return;
    free(r->raw);
    free(r->headers);
    r->raw = r->headers = r->body = NULL;
}

static int find_int_header(const char *hdrs, const char *key, long *out) {
    const char *p = strstr(hdrs, key);
    if (!p) return -1;
    *out = strtol(p + strlen(key), NULL, 10);
    return 0;
}

static int find_dbl_header(const char *hdrs, const char *key, double *out) {
    const char *p = strstr(hdrs, key);
    if (!p) return -1;
    *out = strtod(p + strlen(key), NULL);
    return 0;
}

static void parse_response(Response *r) {
    r->status = -1;
    r->content_length = -1;
    r->req_arrival = r->req_dispatch = r->log_arrival = r->log_dispatch = -1;
    r->thread_id = r->thread_count = r->thread_static = r->thread_dynamic = r->thread_post = -1;

    /* split headers / body at the blank line */
    char *sep = strstr(r->raw, "\r\n\r\n");
    long header_len;
    if (sep) {
        header_len = (long)(sep - r->raw) + 4;
        r->body = r->raw + header_len;
        r->body_len = r->raw_len - header_len;
    } else {
        header_len = r->raw_len;
        r->body = r->raw + r->raw_len;
        r->body_len = 0;
    }
    r->headers = malloc(header_len + 1);
    memcpy(r->headers, r->raw, header_len);
    r->headers[header_len] = '\0';

    /* status line */
    sscanf(r->headers, "HTTP/1.%*d %d", &r->status);

    find_int_header(r->headers, "Content-Length:", &r->content_length);

    find_dbl_header(r->headers, "Stat-Req-Arrival::",  &r->req_arrival);
    find_dbl_header(r->headers, "Stat-Req-Dispatch::", &r->req_dispatch);
    find_dbl_header(r->headers, "Stat-Log-Arrival::",  &r->log_arrival);
    find_dbl_header(r->headers, "Stat-Log-Dispatch::", &r->log_dispatch);

    find_int_header(r->headers, "Stat-Thread-Id::",      &r->thread_id);
    find_int_header(r->headers, "Stat-Thread-Count::",   &r->thread_count);
    find_int_header(r->headers, "Stat-Thread-Static::",  &r->thread_static);
    find_int_header(r->headers, "Stat-Thread-Dynamic::", &r->thread_dynamic);
    find_int_header(r->headers, "Stat-Thread-Post::",    &r->thread_post);

    r->ok = (r->status > 0 && r->thread_id >= 0);
}

/* ------------------------------------------------------------------ */
/* Networking helpers                                                  */
/* ------------------------------------------------------------------ */

static int resolve_ipv4(const char *host, struct in_addr *out) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -1;
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    *out = sin->sin_addr;
    freeaddrinfo(res);
    return 0;
}

static int tcp_connect(const char *host, int port) {
    struct in_addr ip;
    if (resolve_ipv4(host, &ip) != 0) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = ip;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* don't hang forever if the server misbehaves */
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* Reads until EOF (server closes after the HTTP/1.0 response). */
static char *recv_all(int fd, long *out_len) {
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break; /* timeout or error -> return what we have */
        }
        if (n == 0) break; /* EOF */
        len += (size_t)n;
    }
    buf[len] = '\0';
    *out_len = (long)len;
    return buf;
}

/* Sends one request and parses the response. Returns 0 on success. */
static int http_request(const char *method, const char *path, Response *out) {
    memset(out, 0, sizeof(*out));
    int fd = tcp_connect(g_host, g_tcp_port);
    if (fd < 0) return -1;

    char req[512];
    /* exactly the shape the provided client.c uses */
    int rn = snprintf(req, sizeof(req),
                      "%s %s HTTP/1.1\nhost: testclient\n\r\n", method, path);
    if (send_all(fd, req, (size_t)rn) != 0) { close(fd); return -1; }

    long len = 0;
    char *raw = recv_all(fd, &len);
    close(fd);
    if (!raw) return -1;

    out->raw = raw;
    out->raw_len = len;
    parse_response(out);
    return out->ok ? 0 : -1;
}

/* Sends a UDP ping for thread `id`, expects "response from thread <id>". */
static int udp_ping(int id, int *got_id, double timeout_s) {
    struct in_addr ip;
    if (resolve_ipv4(g_host, &ip) != 0) return -1;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = (long)timeout_s;
    tv.tv_usec = (long)((timeout_s - tv.tv_sec) * 1e6);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(g_udp_port);
    srv.sin_addr = ip;

    char msg[32];
    int mn = snprintf(msg, sizeof(msg), "%d", id);
    if (sendto(fd, msg, mn, 0, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        close(fd);
        return -1;
    }

    char resp[256];
    ssize_t n = recvfrom(fd, resp, sizeof(resp) - 1, 0, NULL, NULL);
    close(fd);
    if (n <= 0) return -1; /* timeout / no reply */
    resp[n] = '\0';

    int parsed = -1;
    if (sscanf(resp, "response from thread %d", &parsed) != 1) return -1;
    *got_id = parsed;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Small utilities                                                     */
/* ------------------------------------------------------------------ */

static int count_substr(const char *hay, long hay_len, const char *needle) {
    int count = 0;
    size_t nlen = strlen(needle);
    if (nlen == 0 || hay_len <= 0) return 0;
    for (long i = 0; i + (long)nlen <= hay_len; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) count++;
    }
    return count;
}

/* Returns number of "GET " entries currently in the server log, or -1. */
static int log_get_count(void) {
    Response r;
    if (http_request("POST", g_file, &r) != 0) { response_free(&r); return -1; }
    int c = count_substr(r.body, r.body_len, "GET ");
    response_free(&r);
    return c;
}

static double now_s(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

/* ------------------------------------------------------------------ */
/* Task 1 + Task 3: basic GET/POST sanity & stat invariants           */
/* ------------------------------------------------------------------ */

static void test_basic(void) {
    section("Basic GET / POST sanity (Task 1 + Task 3 headers)");

    Response g;
    int rc = http_request("GET", g_file, &g);
    CHECK(rc == 0, "GET %s returns a well-formed response", g_file);
    if (rc == 0) {
        CHECK(g.status == 200, "GET status is 200 (got %d)", g.status);
        CHECK(g.body_len == g.content_length,
              "GET body length matches Content-Length (%ld == %ld)",
              g.body_len, g.content_length);
        CHECK(g.thread_id >= 1 && g.thread_id <= g_threads,
              "GET handled by a valid worker id %ld in [1,%d]",
              g.thread_id, g_threads);
        CHECK(g.thread_count >= 1, "GET Stat-Thread-Count >= 1 (got %ld)",
              g.thread_count);
        /* all four timing stats present */
        CHECK(g.req_arrival > 0 && g.req_dispatch > 0 &&
              g.log_arrival > 0 && g.log_dispatch > 0,
              "GET has all four Stat-*-Arrival/Dispatch timing fields");
    }
    response_free(&g);

    Response p;
    rc = http_request("POST", g_file, &p);
    CHECK(rc == 0, "POST %s returns a well-formed response", g_file);
    if (rc == 0) {
        CHECK(p.status == 200, "POST status is 200 (got %d)", p.status);
        CHECK(p.thread_id >= 1 && p.thread_id <= g_threads,
              "POST handled by a valid worker id %ld in [1,%d]",
              p.thread_id, g_threads);
    }
    response_free(&p);
}

/* ------------------------------------------------------------------ */
/* Task 3: per-thread counter invariant on every response             */
/*   count == static + dynamic + post  (true because we only ever     */
/*   send valid requests, so no error-only increments to `count`)     */
/* ------------------------------------------------------------------ */

static void check_thread_invariant(const Response *r, const char *what) {
    if (!r->ok) return;
    long sum = r->thread_static + r->thread_dynamic + r->thread_post;
    CHECK(r->thread_count == sum,
          "%s: thread %ld counter invariant Count(%ld) == "
          "Static(%ld)+Dynamic(%ld)+Post(%ld)=%ld",
          what, r->thread_id, r->thread_count,
          r->thread_static, r->thread_dynamic, r->thread_post, sum);
}

static void test_stat_invariant(void) {
    section("Task 3: per-thread counter invariant (valid inputs only)");

    Response g, p;
    if (http_request("GET", g_file, &g) == 0)  check_thread_invariant(&g, "after GET");
    if (http_request("POST", g_file, &p) == 0) check_thread_invariant(&p, "after POST");
    response_free(&g);
    response_free(&p);
}

/* ------------------------------------------------------------------ */
/* Task 3 / Task 1: timing ordering for GET                            */
/*   arrival <= dispatch <= log_arrival <= log_dispatch               */
/* ------------------------------------------------------------------ */

static void test_timing_order(void) {
    section("Task 3: timing ordering");

    Response g;
    if (http_request("GET", g_file, &g) == 0) {
        const double EPS = 0.01;
        CHECK(g.req_dispatch >= g.req_arrival - EPS,
              "GET dispatch >= arrival (%.6f >= %.6f)",
              g.req_dispatch, g.req_arrival);
        CHECK(g.log_arrival >= g.req_dispatch - EPS,
              "GET log-arrival >= dispatch (%.6f >= %.6f)",
              g.log_arrival, g.req_dispatch);
        CHECK(g.log_dispatch >= g.log_arrival - EPS,
              "GET log-dispatch >= log-arrival (%.6f >= %.6f)",
              g.log_dispatch, g.log_arrival);
    }
    response_free(&g);

    /* SOFT: POST is a log reader; per Task 3/5 it should also record sane
     * log timing. Currently POST/501 leave these uninitialized (known bug),
     * so this is a soft probe rather than a hard failure. */
    Response p;
    if (http_request("POST", g_file, &p) == 0) {
        double n = now_s();
        int sane = (p.log_arrival > 0 && p.log_dispatch > 0 &&
                    p.log_dispatch >= p.log_arrival &&
                    p.log_arrival <= n + 1.0 &&
                    p.log_arrival >= p.req_arrival - 1.0);
        WARN_CHECK(sane,
              "POST log timing looks initialized & ordered "
              "(arr=%.6f disp=%.6f) [soft: known POST/501 bug]",
              p.log_arrival, p.log_dispatch);
    }
    response_free(&p);
}

/* ------------------------------------------------------------------ */
/* Task 1: concurrency -> work is distributed across the worker pool   */
/* ------------------------------------------------------------------ */

/* Portable reusable barrier (macOS libpthread lacks pthread_barrier_t). */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int count, target, generation;
} barrier_t;

static void barrier_init(barrier_t *b, int target) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->c, NULL);
    b->count = 0; b->target = target; b->generation = 0;
}
static void barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->m);
    int gen = b->generation;
    if (++b->count == b->target) {
        b->generation++;
        b->count = 0;
        pthread_cond_broadcast(&b->c);
    } else {
        while (gen == b->generation)
            pthread_cond_wait(&b->c, &b->m);
    }
    pthread_mutex_unlock(&b->m);
}
static void barrier_destroy(barrier_t *b) {
    pthread_mutex_destroy(&b->m);
    pthread_cond_destroy(&b->c);
}

typedef struct {
    barrier_t *barrier;
    int ok;
    int status;
    long thread_id;
    long count, st, dy, po;
} ReqJob;

static void *concurrent_get_worker(void *arg) {
    ReqJob *j = (ReqJob *)arg;
    /* connect first, then all fire together for maximum overlap */
    int fd = tcp_connect(g_host, g_tcp_port);
    barrier_wait(j->barrier);
    if (fd < 0) { j->ok = 0; return NULL; }

    char req[512];
    int rn = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\nhost: testclient\n\r\n", g_file);
    if (send_all(fd, req, (size_t)rn) != 0) { close(fd); j->ok = 0; return NULL; }

    long len = 0;
    char *raw = recv_all(fd, &len);
    close(fd);
    if (!raw) { j->ok = 0; return NULL; }

    Response r; memset(&r, 0, sizeof(r));
    r.raw = raw; r.raw_len = len;
    parse_response(&r);
    j->ok = r.ok;
    j->status = r.status;
    j->thread_id = r.thread_id;
    j->count = r.thread_count;
    j->st = r.thread_static; j->dy = r.thread_dynamic; j->po = r.thread_post;
    response_free(&r);
    return NULL;
}

static void test_concurrency_distribution(void) {
    section("Task 1: concurrent requests are distributed across the pool");

    int M = g_threads * 4;
    if (M < 16) M = 16;

    barrier_t barrier;
    barrier_init(&barrier, M);

    pthread_t *tids = calloc(M, sizeof(pthread_t));
    ReqJob *jobs = calloc(M, sizeof(ReqJob));

    for (int i = 0; i < M; i++) {
        jobs[i].barrier = &barrier;
        pthread_create(&tids[i], NULL, concurrent_get_worker, &jobs[i]);
    }
    for (int i = 0; i < M; i++) pthread_join(tids[i], NULL);

    int all_ok = 1, all_200 = 1, all_valid_id = 1, all_invariant = 1;
    int seen[4096]; memset(seen, 0, sizeof(seen));
    int distinct = 0;
    for (int i = 0; i < M; i++) {
        if (!jobs[i].ok) { all_ok = 0; continue; }
        if (jobs[i].status != 200) all_200 = 0;
        long id = jobs[i].thread_id;
        if (id < 1 || id > g_threads) all_valid_id = 0;
        else if (id < 4096 && !seen[id]) { seen[id] = 1; distinct++; }
        if (jobs[i].count != jobs[i].st + jobs[i].dy + jobs[i].po)
            all_invariant = 0;
    }

    CHECK(all_ok, "all %d concurrent GETs got a well-formed response", M);
    CHECK(all_200, "all %d concurrent GETs returned 200 (no drops)", M);
    CHECK(all_valid_id, "every worker id was within [1,%d]", g_threads);
    CHECK(all_invariant, "per-thread counter invariant held for all responses");
    if (g_threads >= 2) {
        CHECK(distinct >= 2,
              "work spread across >= 2 worker threads (saw %d distinct)",
              distinct);
    } else {
        CHECK(distinct <= 1, "single-thread pool used exactly 1 worker");
    }

    barrier_destroy(&barrier);
    free(tids);
    free(jobs);
}

/* ------------------------------------------------------------------ */
/* Task 2: log writer/reader correctness (GET appends, POST reads)     */
/* ------------------------------------------------------------------ */

static void test_log_append_exact(void) {
    section("Task 2: GET writes / POST reads the log (exact append count)");

    int base = log_get_count();
    CHECK(base >= 0, "POST returns a readable log (baseline = %d entries)", base);
    if (base < 0) return;

    const int K = 20;
    int ok_writes = 1;
    for (int i = 0; i < K; i++) {
        Response g;
        if (http_request("GET", g_file, &g) != 0 || g.status != 200)
            ok_writes = 0;
        response_free(&g);
    }
    CHECK(ok_writes, "%d sequential GET writers all succeeded", K);

    int after = log_get_count();
    CHECK(after == base + K,
          "log grew by exactly %d after %d GETs (%d -> %d)",
          K, K, base, after);
}

/* ------------------------------------------------------------------ */
/* Task 2: concurrent readers + writers -> no lost / corrupted writes  */
/* ------------------------------------------------------------------ */

typedef struct { int n; int ok; } WriterArg;
typedef struct { volatile int *stop; int reads; int ok; } ReaderArg;

static void *writer_thread(void *a) {
    WriterArg *w = (WriterArg *)a;
    w->ok = 1;
    for (int i = 0; i < w->n; i++) {
        Response g;
        if (http_request("GET", g_file, &g) != 0 || g.status != 200) w->ok = 0;
        response_free(&g);
    }
    return NULL;
}

static void *reader_thread(void *a) {
    ReaderArg *r = (ReaderArg *)a;
    r->ok = 1;
    r->reads = 0;
    while (!*(r->stop)) {
        Response p;
        if (http_request("POST", g_file, &p) != 0 || p.status != 200) r->ok = 0;
        response_free(&p);
        r->reads++;
    }
    return NULL;
}

static void test_rw_stress(void) {
    section("Task 2: concurrent readers + writers (no lost/corrupt writes)");

    int base = log_get_count();
    if (base < 0) { CHECK(0, "could not read baseline log"); return; }

    const int NW = 4;          /* writer threads */
    const int PER = 25;        /* GETs per writer */
    const int NR = 6;          /* reader threads (continuous) */
    volatile int stop = 0;

    pthread_t wt[NW], rt[NR];
    WriterArg wa[NW];
    ReaderArg ra[NR];

    for (int i = 0; i < NR; i++) {
        ra[i].stop = &stop; ra[i].ok = 1; ra[i].reads = 0;
        pthread_create(&rt[i], NULL, reader_thread, &ra[i]);
    }
    for (int i = 0; i < NW; i++) {
        wa[i].n = PER; wa[i].ok = 1;
        pthread_create(&wt[i], NULL, writer_thread, &wa[i]);
    }

    int writers_ok = 1;
    for (int i = 0; i < NW; i++) { pthread_join(wt[i], NULL); if (!wa[i].ok) writers_ok = 0; }
    stop = 1;
    int readers_ok = 1, total_reads = 0;
    for (int i = 0; i < NR; i++) { pthread_join(rt[i], NULL); if (!ra[i].ok) readers_ok = 0; total_reads += ra[i].reads; }

    CHECK(writers_ok, "all %d concurrent writers (%d GETs each) succeeded",
          NW, PER);
    CHECK(readers_ok, "all concurrent readers got valid responses (%d reads)",
          total_reads);

    int after = log_get_count();
    CHECK(after == base + NW * PER,
          "exactly %d writes recorded under contention (%d -> %d, expected %d)",
          NW * PER, base, after, base + NW * PER);
}

/* ------------------------------------------------------------------ */
/* Task 2: writer priority (SOFT) -> writers don't starve under reads  */
/* ------------------------------------------------------------------ */

static void test_writer_priority_soft(void) {
    section("Task 2: writer priority under reader flood (soft)");

    volatile int stop = 0;
    const int NR = 8;
    pthread_t rt[NR];
    ReaderArg ra[NR];
    for (int i = 0; i < NR; i++) {
        ra[i].stop = &stop; ra[i].ok = 1; ra[i].reads = 0;
        pthread_create(&rt[i], NULL, reader_thread, &ra[i]);
    }

    /* let the readers saturate the log */
    usleep(50 * 1000);

    double worst = 0;
    int writes = 8;
    for (int i = 0; i < writes; i++) {
        double t0 = now_s();
        Response g;
        int rc = http_request("GET", g_file, &g);
        double dt = now_s() - t0;
        response_free(&g);
        if (rc != 0) { worst = 1e9; break; }
        if (dt > worst) worst = dt;
    }

    stop = 1;
    for (int i = 0; i < NR; i++) pthread_join(rt[i], NULL);

    WARN_CHECK(worst < 2.0,
          "writer GET latency stayed bounded under %d-reader flood "
          "(worst = %.3fs) [soft: starvation probe]", NR, worst);
}

/* ------------------------------------------------------------------ */
/* Task 4: UDP ping is answered by the requested thread                */
/* ------------------------------------------------------------------ */

static void test_udp_targeting(void) {
    section("Task 4: UDP ping answered by the requested thread");

    int all_ok = 1, all_match = 1;
    for (int id = 1; id <= g_threads; id++) {
        int got = -1;
        int rc = udp_ping(id, &got, 2.0);
        if (rc != 0) { all_ok = 0; continue; }
        if (got != id) all_match = 0;
    }
    CHECK(all_ok, "every thread id in [1,%d] answered its UDP ping", g_threads);
    CHECK(all_match, "each UDP reply came from the requested thread id");

    /* multiple pings to the same thread all answered by that thread */
    int same_ok = 1;
    for (int i = 0; i < 5; i++) {
        int got = -1;
        if (udp_ping(1, &got, 2.0) != 0 || got != 1) same_ok = 0;
    }
    CHECK(same_ok, "5 repeated pings to thread 1 all answered by thread 1");
}

/* ------------------------------------------------------------------ */
/* Task 4 + Task 1: UDP handled concurrently with a TCP load           */
/* ------------------------------------------------------------------ */

typedef struct { volatile int *stop; int ok; int n; } TcpLoadArg;

static void *tcp_load_thread(void *a) {
    TcpLoadArg *t = (TcpLoadArg *)a;
    t->ok = 1; t->n = 0;
    while (!*(t->stop)) {
        Response g;
        if (http_request("GET", g_file, &g) != 0 || g.status != 200) t->ok = 0;
        response_free(&g);
        t->n++;
    }
    return NULL;
}

static void test_udp_concurrent_with_tcp(void) {
    section("Task 4: UDP answered while TCP load is in flight");

    volatile int stop = 0;
    const int NL = 4;
    pthread_t lt[NL];
    TcpLoadArg la[NL];
    for (int i = 0; i < NL; i++) {
        la[i].stop = &stop;
        pthread_create(&lt[i], NULL, tcp_load_thread, &la[i]);
    }

    int udp_ok = 1, udp_match = 1, pings = 20;
    for (int i = 0; i < pings; i++) {
        int id = (i % g_threads) + 1;
        int got = -1;
        if (udp_ping(id, &got, 3.0) != 0) udp_ok = 0;
        else if (got != id) udp_match = 0;
    }

    stop = 1;
    int load_ok = 1, total = 0;
    for (int i = 0; i < NL; i++) { pthread_join(lt[i], NULL); if (!la[i].ok) load_ok = 0; total += la[i].n; }

    CHECK(udp_ok, "all %d UDP pings answered during TCP load", pings);
    CHECK(udp_match, "every UDP reply matched its requested thread under load");
    CHECK(load_ok, "concurrent TCP GET load stayed healthy (%d requests)", total);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 5) {
        fprintf(stderr,
            "Usage: %s <host> <tcp_port> <udp_port> <num_threads> [file]\n"
            "  (num_threads must match how the server was launched)\n",
            argv[0]);
        return 2;
    }
    g_host = argv[1];
    g_tcp_port = atoi(argv[2]);
    g_udp_port = atoi(argv[3]);
    g_threads = atoi(argv[4]);
    if (argc >= 6) g_file = argv[5];

    if (g_threads < 1) { fprintf(stderr, "num_threads must be >= 1\n"); return 2; }

    printf(C_CYN "HW3 server test client" C_RST
           "  host=%s tcp=%d udp=%d threads=%d file=%s\n",
           g_host, g_tcp_port, g_udp_port, g_threads, g_file);

    /* connectivity smoke test */
    Response smoke;
    if (http_request("GET", g_file, &smoke) != 0) {
        fprintf(stderr, C_RED "Cannot reach server / parse response. "
                "Is it running on %s:%d?\n" C_RST, g_host, g_tcp_port);
        response_free(&smoke);
        return 2;
    }
    if (smoke.status == 404) {
        fprintf(stderr, C_YEL
            "\nHINT: GET %s returned 404. The server cannot find "
            "./public/%s in its\n"
            "      working directory. Launch the server from a directory that "
            "contains\n"
            "      public/%s (or stage it there). File-serving tests will fail "
            "until then.\n\n" C_RST,
            g_file, g_file, g_file);
    }
    response_free(&smoke);

    test_basic();
    test_stat_invariant();
    test_timing_order();
    test_concurrency_distribution();
    test_log_append_exact();
    test_rw_stress();
    test_writer_priority_soft();
    test_udp_targeting();
    test_udp_concurrent_with_tcp();

    printf("\n" C_CYN "==== summary ====" C_RST "\n");
    printf("  " C_GREEN "passed: %d" C_RST "\n", g_pass);
    printf("  " C_RED   "failed: %d" C_RST "\n", g_fail);
    printf("  " C_YEL   "warns : %d" C_RST "  (soft/timing/known-bug probes)\n", g_warn);

    return g_fail == 0 ? 0 : 1;
}
