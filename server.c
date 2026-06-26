#include "segel.h"
#include "request.h"
#include "log.h"
#include "requests_queue.h"

#define MAX_THREADS 3

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// Parses command-line arguments
void getargs(int *port, int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
}

// TODO: HW3 — Task 1: Initialize the thread pool and request queue.
// This server currently handles all requests in the main thread.

// TODO: HW3 — Task 4: Add the UDP channel (see the UDP_* wrappers in segel.c).

// TODO: HW3 — Extend getargs() to parse the full argument list.

void process_request(int connfd, server_log log) {
    // TODO: HW3 — Record the request arrival time here.

    // DEMO PURPOSE ONLY:
    // This is a dummy request handler that immediately processes the
    // request in the master thread without concurrency. Replace this with
    // logic that enqueues the connection so a worker thread handles it.

    threads_stats t = malloc(sizeof(struct Threads_stats));
    t->id = 0; // Thread ID (placeholder)
    t->stat_req = 0; // Static request count
    t->dynm_req = 0; // Dynamic request count
    t->post_req = 0; // POST request count
    t->total_req = 0; // Total request count

    time_stats dum;

    // gettimeofday(&arrival, NULL);

    // Call the request handler (immediate in master thread — DEMO ONLY)
    requestHandle(connfd, dum, t, log);

    free(t); // Cleanup
}

pthread_cond_t has_requests = PTHREAD_COND_INITIALIZER;

pthread_mutex_t requests_q_mutex = PTHREAD_MUTEX_INITIALIZER;


struct thread_ctx {
    server_log log;
    rqueue_t *rqueue;
};

typedef struct thread_ctx thread_ctx_t;

void *processor_entry(void *thread_ctx_p) {
    printf("thread created\n");

    thread_ctx_t *thread_ctx = (thread_ctx_t *) thread_ctx_p;

    // not busy waiting but it means the thread is active
    while (1) {
        // otherwise let's pop the first request, but make sure to lock
        pthread_mutex_lock(&requests_q_mutex);

        // if no requests are avilable then enter blocking
        while (is_queue_empty(thread_ctx->rqueue)) {
            pthread_cond_wait(&has_requests, &requests_q_mutex);
        }


        int connfd = rqueue_pop(thread_ctx->rqueue);
        pthread_mutex_unlock(&requests_q_mutex);


        process_request(connfd, thread_ctx->log);
        Close(connfd); // Close the connection
    }

    return NULL;
}

pthread_t threads[MAX_THREADS];

void init_threads_pool(thread_ctx_t *threads_ctx) {
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_t thread_id;

        // we need to create a thread and add it to the pool
        pthread_create(&thread_id, NULL, processor_entry, threads_ctx);

        // bookkeep
        threads[i] = thread_id;
    }
}

int main(int argc, char *argv[]) {
    // if weired print stuff happens it's because of fucking buffering
    // setbuf(stdout, NULL);


    // Create the global server log
    server_log log = create_log();

    thread_ctx_t *thread_ctx = malloc(sizeof(thread_ctx_t));

    thread_ctx->rqueue = (rqueue_t *) malloc(sizeof(rqueue_t));
    thread_ctx->log = log;

    // init the queue
    init_rqueue(thread_ctx->rqueue);


    // init threads with ctx
    init_threads_pool(thread_ctx);

    int port, clientlen;
    struct sockaddr_in clientaddr;

    getargs(&port, argc, argv);

    int listenfd = Open_listenfd(port);
    clientlen = sizeof(clientaddr);

    // this is not busy waiting rather the server is active
    while (1) {
        int connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);
        //connected accepted - encque it.

        // so we aquire the lock
        pthread_mutex_lock(&requests_q_mutex);

        rqueue_append(thread_ctx->rqueue, connfd);

        pthread_mutex_unlock(&requests_q_mutex);

        // then we tell everybody the state changed
        pthread_cond_signal(&has_requests);

        // process_request(connfd, log);
    }

    // Clean up the server log before exiting
    destroy_log(log);

    // TODO: HW3 — Add cleanup code for the thread pool and queue.
}
