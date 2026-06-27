/*
 * tests/stress_client.c
 *
 * Stress / edge-case / performance harness for the HW3 web server.
 *
 * Complements test_client.c (which checks functional correctness). This file:
 *   - drives heavy concurrent load (GET / POST / mixed),
 *   - exercises edge cases with VALID inputs only (directory index "/",
 *     binary static asset, queue saturation, very high concurrency,
 *     log-size scaling, UDP floods),
 *   - reports PERFORMANCE METRICS:
 *       * throughput (req/s),
 *       * client-measured latency (min/mean/p50/p90/p99/max),
 *       * server-side queue wait  = Stat-Req-Dispatch - Stat-Req-Arrival,
 *       * server-side log time    = Stat-Log-Dispatch - Stat-Log-Arrival (GET),
 *       * per-worker load distribution + fairness (via Stat-Thread-Id),
 *       * UDP round-trip latency & loss.
 *
 * A watchdog alarm aborts a phase that hangs (deadlock detector).
 *
 * Build:  make stress_client
 * Run:    ./stress_client <host> <tcp_port> <udp_port> <num_threads> [file] [scale]
 *   scale (default 1) multiplies request counts for longer runs.
 *
 * Server must be launched with a matching thread count, e.g.
 *   ./server 5003 5004 8 16 0
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
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>

#define C_GREEN "\x1b[32m"
#define C_RED   "\x1b[31m"
#define C_YEL   "\x1b[33m"
#define C_CYN   "\x1b[36m"
#define C_RST   "\x1b[0m"

#define MAX_TH 1024

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...)                                            \
    do {                                                           \
        if (cond) { g_pass++; printf("  " C_GREEN "PASS" C_RST " "); }\
        else      { g_fail++; printf("  " C_RED   "FAIL" C_RST " "); }\
        printf(__VA_ARGS__); printf("\n");                         \
    } while (0)

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

static const char *g_host;
static int g_tcp_port, g_udp_port, g_threads;
static const char *g_file = "home.html";
static int g_scale = 1;

/* ------------------------------------------------------------------ */
/* Response parsing                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int    ok, status;
    long   content_length;
    double req_arrival, req_dispatch, log_arrival, log_dispatch;
    long   thread_id, thread_count, thread_static, thread_dynamic, thread_post;
    char  *raw; long raw_len;
    char  *headers; char *body; long body_len;
} Response;

static void response_free(Response *r) {
    if (!r) return;
    free(r->raw); free(r->headers);
    r->raw = r->headers = r->body = NULL;
}

static int find_int_header(const char *h, const char *k, long *o) {
    const char *p = strstr(h, k); if (!p) return -1;
    *o = strtol(p + strlen(k), NULL, 10); return 0;
}
static int find_dbl_header(const char *h, const char *k, double *o) {
    const char *p = strstr(h, k); if (!p) return -1;
    *o = strtod(p + strlen(k), NULL); return 0;
}

static void parse_response(Response *r) {
    r->status = -1; r->content_length = -1;
    r->req_arrival = r->req_dispatch = r->log_arrival = r->log_dispatch = -1;
    r->thread_id = r->thread_count = r->thread_static = r->thread_dynamic = r->thread_post = -1;

    char *sep = strstr(r->raw, "\r\n\r\n");
    long header_len;
    if (sep) { header_len = (long)(sep - r->raw) + 4; r->body = r->raw + header_len; r->body_len = r->raw_len - header_len; }
    else     { header_len = r->raw_len; r->body = r->raw + r->raw_len; r->body_len = 0; }

    r->headers = malloc(header_len + 1);
    memcpy(r->headers, r->raw, header_len);
    r->headers[header_len] = '\0';

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
/* Networking                                                          */
/* ------------------------------------------------------------------ */

static int resolve_ipv4(const char *host, struct in_addr *out) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -1;
    *out = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

/* How many sockets/threads we can safely use, derived from the fd limit. */
static int fd_budget(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return 200;
    long b = (long)rl.rlim_cur - 32;   /* reserve some for stdio/dns/etc. */
    if (b < 8) b = 8;
    if (b > 100000) b = 100000;
    return (int)b;
}

static int tcp_connect(const char *host, int port) {
    struct in_addr ip;
    if (resolve_ipv4(host, &ip) != 0) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr = ip;

    /* connect with a timeout so a stalled connect can never hang a worker
     * forever (which previously could wedge a client-side barrier). */
    if (fd < FD_SETSIZE) {
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, (struct sockaddr *)&a, sizeof(a));
        if (rc < 0) {
            if (errno != EINPROGRESS) { close(fd); return -1; }
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval ctv = { .tv_sec = 8, .tv_usec = 0 };
            if (select(fd + 1, NULL, &wf, NULL, &ctv) <= 0) { close(fd); return -1; }
            int err = 0; socklen_t el = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err != 0) { close(fd); return -1; }
        }
        fcntl(fd, F_SETFL, fl);   /* restore blocking */
    } else {
        if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
    }

    struct timeval tv = { .tv_sec = 15, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)n;
    }
    return 0;
}

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
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    *out_len = (long)len;
    return buf;
}

static int http_request_path(const char *method, const char *path, Response *out) {
    memset(out, 0, sizeof(*out));
    int fd = tcp_connect(g_host, g_tcp_port);
    if (fd < 0) return -1;
    char req[512];
    int rn = snprintf(req, sizeof(req),
                      "%s %s HTTP/1.1\nhost: stressclient\n\r\n", method, path);
    if (send_all(fd, req, (size_t)rn) != 0) { close(fd); return -1; }
    long len = 0;
    char *raw = recv_all(fd, &len);
    close(fd);
    if (!raw) return -1;
    out->raw = raw; out->raw_len = len;
    parse_response(out);
    return out->ok ? 0 : -1;
}

static int http_request(const char *method, Response *out) {
    return http_request_path(method, g_file, out);
}

static int udp_ping(int id, int *got_id, double timeout_s) {
    struct in_addr ip;
    if (resolve_ipv4(g_host, &ip) != 0) return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct timeval tv;
    tv.tv_sec = (long)timeout_s;
    tv.tv_usec = (long)((timeout_s - tv.tv_sec) * 1e6);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons(g_udp_port); srv.sin_addr = ip;
    char msg[32];
    int mn = snprintf(msg, sizeof(msg), "%d", id);
    if (sendto(fd, msg, mn, 0, (struct sockaddr *)&srv, sizeof(srv)) < 0) { close(fd); return -1; }
    char resp[256];
    ssize_t n = recvfrom(fd, resp, sizeof(resp) - 1, 0, NULL, NULL);
    close(fd);
    if (n <= 0) return -1;
    resp[n] = '\0';
    int parsed = -1;
    if (sscanf(resp, "response from thread %d", &parsed) != 1) return -1;
    if (got_id) *got_id = parsed;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Utilities + stats                                                   */
/* ------------------------------------------------------------------ */

static double now_s(void) {
    struct timeval t; gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec / 1e6;
}

static int count_substr(const char *hay, long hay_len, const char *needle) {
    int c = 0; size_t nl = strlen(needle);
    if (!nl || hay_len <= 0) return 0;
    for (long i = 0; i + (long)nl <= hay_len; i++)
        if (memcmp(hay + i, needle, nl) == 0) c++;
    return c;
}

static int log_get_count(void) {
    Response r;
    if (http_request("POST", &r) != 0) { response_free(&r); return -1; }
    int c = count_substr(r.body, r.body_len, "GET ");
    response_free(&r);
    return c;
}

typedef struct { int n; double min, mean, p50, p90, p99, max; } stats_t;

static int cmp_dbl(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

static stats_t stats_of(const double *src, int n) {
    stats_t s; memset(&s, 0, sizeof(s)); s.n = n;
    if (n <= 0) return s;
    double *a = malloc((size_t)n * sizeof(double));
    memcpy(a, src, (size_t)n * sizeof(double));
    qsort(a, n, sizeof(double), cmp_dbl);
    double sum = 0; for (int i = 0; i < n; i++) sum += a[i];
    s.min = a[0]; s.max = a[n - 1]; s.mean = sum / n;
    s.p50 = a[(int)(0.50 * (n - 1))];
    s.p90 = a[(int)(0.90 * (n - 1))];
    s.p99 = a[(int)(0.99 * (n - 1))];
    free(a);
    return s;
}

static void print_stats(const char *name, stats_t s) {
    if (s.n <= 0) { printf("    %-12s (no samples)\n", name); return; }
    printf("    %-12s n=%-6d min=%.3f mean=%.3f p50=%.3f p90=%.3f p99=%.3f max=%.3f ms\n",
           name, s.n, s.min, s.mean, s.p50, s.p90, s.p99, s.max);
}

static void section(const char *t) { printf("\n" C_CYN "== %s ==" C_RST "\n", t); }

/* watchdog: abort a phase that hangs (deadlock detector) */
static void on_alarm(int sig) {
    (void)sig;
    const char *m = "\n" C_RED "WATCHDOG: phase exceeded time limit "
                    "(possible deadlock/livelock) — aborting\n" C_RST;
    write(2, m, strlen(m));
    _exit(3);
}
static void watchdog(unsigned secs) { alarm(secs); }
static void watchdog_off(void) { alarm(0); }

/* ------------------------------------------------------------------ */
/* Closed-loop load generator                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int   reqs;
    int   mix;        /* % GET (100 = all GET, 0 = all POST) */
    unsigned seed;
    /* outputs */
    double *lat, *qw, *lt;
    int     ln, qn, tn;
    int     errors;
    long    dist[MAX_TH];
} wres_t;

static void *load_worker(void *arg) {
    wres_t *w = (wres_t *)arg;
    for (int i = 0; i < w->reqs; i++) {
        int do_get = (int)(rand_r(&w->seed) % 100) < w->mix;
        const char *m = do_get ? "GET" : "POST";
        double t0 = now_s();
        Response r;
        int rc = http_request(m, &r);
        double dt = (now_s() - t0) * 1000.0;
        if (rc != 0 || r.status != 200) { w->errors++; response_free(&r); continue; }
        w->lat[w->ln++] = dt;
        if (r.req_arrival > 0 && r.req_dispatch > 0)
            w->qw[w->qn++] = (r.req_dispatch - r.req_arrival) * 1000.0;
        if (do_get && r.log_arrival > 0 && r.log_dispatch > 0)
            w->lt[w->tn++] = (r.log_dispatch - r.log_arrival) * 1000.0;
        if (r.thread_id >= 1 && r.thread_id < MAX_TH) w->dist[r.thread_id]++;
        response_free(&r);
    }
    return NULL;
}

static void run_load(const char *title, int nthreads, int reqs_per, int mix) {
    int budget = fd_budget();
    if (nthreads > budget) {
        printf("    (capping concurrency %d -> %d to fit the fd budget)\n", nthreads, budget);
        nthreads = budget;
    }
    if (nthreads < 1) nthreads = 1;
    int total = nthreads * reqs_per;
    printf(C_CYN "\n[load] %s" C_RST
           "  conc=%d  total=%d  mix=%d%%GET/%d%%POST\n",
           title, nthreads, total, mix, 100 - mix);

    pthread_t *th = calloc(nthreads, sizeof(pthread_t));
    wres_t    *wr = calloc(nthreads, sizeof(wres_t));
    for (int i = 0; i < nthreads; i++) {
        wr[i].reqs = reqs_per;
        wr[i].mix = mix;
        wr[i].seed = (unsigned)(i * 2654435761u + 12345u);
        wr[i].lat = malloc((size_t)reqs_per * sizeof(double));
        wr[i].qw  = malloc((size_t)reqs_per * sizeof(double));
        wr[i].lt  = malloc((size_t)reqs_per * sizeof(double));
    }

    watchdog(120);
    double start = now_s();
    for (int i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, load_worker, &wr[i]);
    for (int i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double dur = now_s() - start;
    watchdog_off();

    /* aggregate */
    double *lat = malloc((size_t)total * sizeof(double));
    double *qw  = malloc((size_t)total * sizeof(double));
    double *lt  = malloc((size_t)total * sizeof(double));
    int ln = 0, qn = 0, tn = 0, errors = 0;
    long dist[MAX_TH]; memset(dist, 0, sizeof(dist));
    for (int i = 0; i < nthreads; i++) {
        memcpy(lat + ln, wr[i].lat, (size_t)wr[i].ln * sizeof(double)); ln += wr[i].ln;
        memcpy(qw  + qn, wr[i].qw,  (size_t)wr[i].qn * sizeof(double)); qn += wr[i].qn;
        memcpy(lt  + tn, wr[i].lt,  (size_t)wr[i].tn * sizeof(double)); tn += wr[i].tn;
        errors += wr[i].errors;
        for (int t = 0; t < MAX_TH; t++) dist[t] += wr[i].dist[t];
        free(wr[i].lat); free(wr[i].qw); free(wr[i].lt);
    }

    int ok = total - errors;
    printf("    throughput : " C_GREEN "%.0f req/s" C_RST
           "   (%d ok / %d, %d errors, %.3fs)\n",
           dur > 0 ? ok / dur : 0.0, ok, total, errors, dur);
    print_stats("latency", stats_of(lat, ln));
    print_stats("queue-wait", stats_of(qw, qn));
    if (tn > 0) print_stats("log-crit", stats_of(lt, tn));

    /* per-worker distribution + fairness */
    long mn = -1, mx = 0; int active = 0;
    for (int t = 1; t <= g_threads && t < MAX_TH; t++) {
        if (dist[t] > mx) mx = dist[t];
        if (mn < 0 || dist[t] < mn) mn = dist[t];
        if (dist[t] > 0) active++;
    }
    printf("    distribution:");
    for (int t = 1; t <= g_threads && t < MAX_TH; t++) printf(" t%d=%ld", t, dist[t]);
    printf("\n    fairness   : %d/%d workers used, min/max=%.2f (1.0=perfect)\n",
           active, g_threads, mx > 0 ? (double)mn / (double)mx : 0.0);

    CHECK(errors == 0, "%s: no failed requests under load", title);

    free(lat); free(qw); free(lt); free(th); free(wr);
}

/* ------------------------------------------------------------------ */
/* Edge: valid static assets (directory index + binary file)           */
/* ------------------------------------------------------------------ */

static void test_edge_valid_assets(void) {
    section("Edge (valid inputs): directory index & binary asset");

    Response r;
    if (http_request_path("GET", "/", &r) == 0) {
        CHECK(r.status == 200, "GET \"/\" serves the directory index (status %d)", r.status);
        CHECK(r.body_len == r.content_length,
              "GET \"/\" body matches Content-Length (%ld==%ld)", r.body_len, r.content_length);
    } else {
        CHECK(0, "GET \"/\" returned a well-formed response");
    }
    response_free(&r);

    Response f;
    if (http_request_path("GET", "favicon.ico", &f) == 0) {
        CHECK(f.status == 200, "GET favicon.ico (binary) status 200 (got %d)", f.status);
        CHECK(f.body_len == f.content_length,
              "favicon.ico body matches Content-Length (%ld==%ld)", f.body_len, f.content_length);
    } else {
        CHECK(0, "GET favicon.ico returned a well-formed response");
    }
    response_free(&f);
}

/* ------------------------------------------------------------------ */
/* Edge/stress: connection saturation & backpressure (no drops)        */
/*   Open many simultaneous connections, fire together. The bounded     */
/*   queue must make the master block, not drop — all must get 200.     */
/* ------------------------------------------------------------------ */

/* A start GATE (not a counting barrier): workers connect, then all wait for a
 * single broadcast. It needs no exact arrival count, so it cannot deadlock if
 * some client threads fail to start or a connect is slow. */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int go; } gate_t;
static void gate_init(gate_t *g){ pthread_mutex_init(&g->m,NULL); pthread_cond_init(&g->c,NULL); g->go=0; }
static void gate_wait(gate_t *g){ pthread_mutex_lock(&g->m); while(!g->go) pthread_cond_wait(&g->c,&g->m); pthread_mutex_unlock(&g->m); }
static void gate_open(gate_t *g){ pthread_mutex_lock(&g->m); g->go=1; pthread_cond_broadcast(&g->c); pthread_mutex_unlock(&g->m); }
static void gate_destroy(gate_t *g){ pthread_mutex_destroy(&g->m); pthread_cond_destroy(&g->c); }

typedef struct {
    gate_t *gate;
    int ok, status; double qwait_ms;
} sat_job;

static void *sat_worker(void *arg) {
    sat_job *j = (sat_job *)arg;
    int fd = tcp_connect(g_host, g_tcp_port);
    gate_wait(j->gate);            /* all fire together once main opens the gate */
    if (fd < 0) { j->ok = 0; return NULL; }
    char req[512];
    int rn = snprintf(req, sizeof(req), "GET %s HTTP/1.1\nhost: stress\n\r\n", g_file);
    if (send_all(fd, req, (size_t)rn) != 0) { close(fd); j->ok = 0; return NULL; }
    long len = 0; char *raw = recv_all(fd, &len);
    close(fd);
    if (!raw) { j->ok = 0; return NULL; }
    Response r; memset(&r, 0, sizeof(r)); r.raw = raw; r.raw_len = len; parse_response(&r);
    j->ok = r.ok; j->status = r.status;
    j->qwait_ms = (r.req_arrival > 0 && r.req_dispatch > 0)
                  ? (r.req_dispatch - r.req_arrival) * 1000.0 : -1;
    response_free(&r);
    return NULL;
}

static void test_saturation(void) {
    section("Edge/stress: connection saturation & backpressure");

    /* Open many MORE simultaneous connections than (queue_size + threads) so
     * the bounded queue must block, not drop. This is intentionally NOT scaled
     * by g_scale, and is capped to the fd budget so the client can't exhaust
     * descriptors/threads (which used to wedge the rendezvous). */
    int N = g_threads * 16;
    if (N < 64) N = 64;
    if (N > 256) N = 256;
    int budget = fd_budget();
    if (N > budget) N = budget;
    printf("    opening %d simultaneous connections (fd budget=%d; "
           "queue must block, not drop)\n", N, budget);

    gate_t gate; gate_init(&gate);
    pthread_t *th = calloc(N, sizeof(pthread_t));
    sat_job *jb = calloc(N, sizeof(sat_job));
    for (int i = 0; i < N; i++) jb[i].gate = &gate;

    watchdog(120);
    int created = 0;
    for (int i = 0; i < N; i++) {
        if (pthread_create(&th[i], NULL, sat_worker, &jb[i]) == 0) created++;
        else break;                 /* hit a thread limit — proceed with what we have */
    }
    usleep(250 * 1000);             /* let workers establish their connections */
    double t0 = now_s();
    gate_open(&gate);               /* release them all at once */
    for (int i = 0; i < created; i++) pthread_join(th[i], NULL);
    double dur = now_s() - t0;
    watchdog_off();

    if (created < N)
        printf("    note: created %d/%d client threads (system limit)\n", created, N);

    int ok = 0, got200 = 0, qn = 0;
    double *qw = malloc((size_t)(created ? created : 1) * sizeof(double));
    for (int i = 0; i < created; i++) {
        if (jb[i].ok) ok++;
        if (jb[i].status == 200) got200++;
        if (jb[i].qwait_ms >= 0) qw[qn++] = jb[i].qwait_ms;
    }
    printf("    served %d/%d in %.3fs (%.0f req/s)\n",
           got200, created, dur, dur > 0 ? got200 / dur : 0);
    print_stats("queue-wait", stats_of(qw, qn));
    CHECK(created > 0 && ok == created,
          "all %d saturating connections got a response (no drops)", created);
    CHECK(created > 0 && got200 == created,
          "all %d saturating connections returned 200", created);

    free(qw); free(th); free(jb);
    gate_destroy(&gate);
}

/* ------------------------------------------------------------------ */
/* Edge/stress: very high concurrency (deadlock / fd-exhaustion probe)  */
/* ------------------------------------------------------------------ */

static void test_high_concurrency(void) {
    section("Edge/stress: very high concurrency (deadlock probe)");
    /* concurrency is capped to the fd budget inside run_load; scale the work
     * via requests-per-thread instead of by spawning unbounded threads. */
    int conc = 200;
    int per = 5 * g_scale;
    run_load("high-concurrency mix", conc, per, 70);
}

/* ------------------------------------------------------------------ */
/* Perf: POST (log read) latency vs log size                           */
/* ------------------------------------------------------------------ */

static double median_post_ms(int samples) {
    double *a = malloc((size_t)samples * sizeof(double));
    int n = 0;
    for (int i = 0; i < samples; i++) {
        double t0 = now_s();
        Response r;
        int rc = http_request("POST", &r);
        double dt = (now_s() - t0) * 1000.0;
        if (rc == 0 && r.status == 200) a[n++] = dt;
        response_free(&r);
    }
    stats_t s = stats_of(a, n);
    free(a);
    return s.p50;
}

static void test_post_scaling(void) {
    section("Perf: POST (log read) latency vs log size");

    int before = log_get_count();
    double t_before = median_post_ms(15);
    printf("    log=%d entries -> POST p50 = %.3f ms\n", before, t_before);

    int grow = 1000 * g_scale;
    printf("    growing log by %d GET entries ...\n", grow);
    watchdog(180);
    run_load("log-grow writers", g_threads * 2, grow / (g_threads * 2), 100);
    watchdog_off();

    int after = log_get_count();
    double t_after = median_post_ms(15);
    printf("    log=%d entries -> POST p50 = %.3f ms\n", after, t_after);
    printf("    " C_YEL "observation:" C_RST " log grew %d->%d entries, "
           "POST p50 %.3f->%.3f ms (reader cost vs log size)\n",
           before, after, t_before, t_after);
}

/* ------------------------------------------------------------------ */
/* Perf: UDP round-trip latency & loss                                 */
/* ------------------------------------------------------------------ */

static void test_udp_rtt_sequential(void) {
    section("Perf: UDP sequential RTT");
    int N = 200 * g_scale;
    double *rtt = malloc((size_t)N * sizeof(double));
    int n = 0, loss = 0, mism = 0;
    watchdog(120);
    double t0 = now_s();
    for (int i = 0; i < N; i++) {
        int id = (i % g_threads) + 1, got = -1;
        double s = now_s();
        if (udp_ping(id, &got, 1.0) != 0) { loss++; continue; }
        if (got != id) mism++;
        rtt[n++] = (now_s() - s) * 1000.0;
    }
    double dur = now_s() - t0;
    watchdog_off();
    printf("    %d pings, %d loss, %d mismatched, %.0f pings/s\n",
           N, loss, mism, dur > 0 ? (N - loss) / dur : 0);
    print_stats("udp-rtt", stats_of(rtt, n));
    CHECK(loss == 0, "no UDP pings were lost (sequential)");
    CHECK(mism == 0, "every UDP reply matched its requested thread");
    free(rtt);
}

typedef struct { int pings; unsigned seed; int loss, mism, n; double *rtt; } udp_w;
static void *udp_worker(void *arg) {
    udp_w *w = (udp_w *)arg;
    for (int i = 0; i < w->pings; i++) {
        int id = (int)(rand_r(&w->seed) % g_threads) + 1, got = -1;
        double s = now_s();
        if (udp_ping(id, &got, 2.0) != 0) { w->loss++; continue; }
        if (got != id) w->mism++;
        w->rtt[w->n++] = (now_s() - s) * 1000.0;
    }
    return NULL;
}

static void test_udp_concurrent(void) {
    section("Perf: UDP concurrent flood");
    int C = g_threads * 2, per = 50 * g_scale;
    pthread_t *th = calloc(C, sizeof(pthread_t));
    udp_w *w = calloc(C, sizeof(udp_w));
    for (int i = 0; i < C; i++) {
        w[i].pings = per; w[i].seed = (unsigned)(i * 40503u + 7u);
        w[i].rtt = malloc((size_t)per * sizeof(double));
    }
    watchdog(120);
    double t0 = now_s();
    for (int i = 0; i < C; i++) pthread_create(&th[i], NULL, udp_worker, &w[i]);
    for (int i = 0; i < C; i++) pthread_join(th[i], NULL);
    double dur = now_s() - t0;
    watchdog_off();

    int total = C * per, loss = 0, mism = 0, rn = 0;
    double *rtt = malloc((size_t)total * sizeof(double));
    for (int i = 0; i < C; i++) {
        loss += w[i].loss; mism += w[i].mism;
        memcpy(rtt + rn, w[i].rtt, (size_t)w[i].n * sizeof(double)); rn += w[i].n;
        free(w[i].rtt);
    }
    printf("    %d pings across %d threads, %d loss, %d mismatched, %.0f pings/s\n",
           total, C, loss, mism, dur > 0 ? (total - loss) / dur : 0);
    print_stats("udp-rtt", stats_of(rtt, rn));
    CHECK(mism == 0, "every concurrent UDP reply matched its requested thread");
    free(rtt); free(th); free(w);
}

/* UDP RTT measured while a heavy TCP load runs (interference metric) */
typedef struct { volatile int *stop; int n; } tcpbg;
static void *tcp_bg(void *a) {
    tcpbg *t = (tcpbg *)a;
    while (!*(t->stop)) { Response r; if (http_request("GET", &r) == 0) t->n++; response_free(&r); }
    return NULL;
}

static void test_udp_under_tcp(void) {
    section("Perf: UDP RTT under heavy TCP load (interference)");
    volatile int stop = 0;
    int NL = g_threads * 2;
    pthread_t *lt = calloc(NL, sizeof(pthread_t));
    tcpbg *la = calloc(NL, sizeof(tcpbg));
    for (int i = 0; i < NL; i++) { la[i].stop = &stop; pthread_create(&lt[i], NULL, tcp_bg, &la[i]); }

    int N = 100 * g_scale, n = 0, loss = 0, mism = 0;
    double *rtt = malloc((size_t)N * sizeof(double));
    watchdog(120);
    for (int i = 0; i < N; i++) {
        int id = (i % g_threads) + 1, got = -1;
        double s = now_s();
        if (udp_ping(id, &got, 2.0) != 0) { loss++; continue; }
        if (got != id) mism++;
        rtt[n++] = (now_s() - s) * 1000.0;
    }
    watchdog_off();
    stop = 1;
    long bg = 0;
    for (int i = 0; i < NL; i++) { pthread_join(lt[i], NULL); bg += la[i].n; }

    printf("    %d UDP pings during %ld background TCP GETs, %d loss, %d mismatched\n",
           N, bg, loss, mism);
    print_stats("udp-rtt", stats_of(rtt, n));
    CHECK(loss == 0, "no UDP loss under heavy TCP load");
    CHECK(mism == 0, "UDP replies still correctly targeted under load");
    free(rtt); free(lt); free(la);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, on_alarm);

    if (argc < 5) {
        fprintf(stderr,
            "Usage: %s <host> <tcp_port> <udp_port> <num_threads> [file] [scale]\n",
            argv[0]);
        return 2;
    }
    g_host = argv[1];
    g_tcp_port = atoi(argv[2]);
    g_udp_port = atoi(argv[3]);
    g_threads = atoi(argv[4]);
    if (argc >= 6) g_file = argv[5];
    if (argc >= 7) g_scale = atoi(argv[6]);
    if (g_scale < 1) g_scale = 1;
    if (g_threads < 1) { fprintf(stderr, "num_threads must be >= 1\n"); return 2; }

    printf(C_CYN "HW3 stress / perf client" C_RST
           "  host=%s tcp=%d udp=%d threads=%d file=%s scale=%d\n",
           g_host, g_tcp_port, g_udp_port, g_threads, g_file, g_scale);

    /* smoke + 404 hint */
    Response smoke;
    if (http_request("GET", &smoke) != 0) {
        fprintf(stderr, C_RED "Cannot reach server on %s:%d\n" C_RST, g_host, g_tcp_port);
        response_free(&smoke);
        return 2;
    }
    if (smoke.status == 404)
        fprintf(stderr, C_YEL "HINT: GET %s -> 404. Run the server from a dir "
                "containing public/%s, else GET/perf numbers will be error-dominated.\n" C_RST,
                g_file, g_file);
    response_free(&smoke);

    int conc = g_threads * 2;
    int per  = 200 * g_scale;

    test_edge_valid_assets();

    run_load("GET throughput (all writers)",  conc, per, 100);
    run_load("POST throughput (all readers)", conc, per, 0);
    run_load("mixed 80/20 GET/POST",          conc, per, 80);

    test_saturation();
    test_high_concurrency();
    test_post_scaling();

    test_udp_rtt_sequential();
    test_udp_concurrent();
    test_udp_under_tcp();

    printf("\n" C_CYN "==== summary ====" C_RST "\n");
    printf("  " C_GREEN "passed: %d" C_RST "   " C_RED "failed: %d" C_RST "\n", g_pass, g_fail);
    printf("  (metrics above are informational; FAIL means a correctness/"
           "robustness assertion broke)\n");
    return g_fail == 0 ? 0 : 1;
}
