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

pthread_cond_t has_requests = PTHREAD_COND_INITIALIZER;

pthread_mutex_t requests_q_mutex = PTHREAD_MUTEX_INITIALIZER;


struct thread_ctx {
    server_log log;
    rqueue_t *rqueue;
    int id;
};

typedef struct thread_ctx thread_ctx_t;

void *processor_entry(void *thread_ctx_p) {

    thread_ctx_t *thread_ctx = (thread_ctx_t *) thread_ctx_p;

    //TODO: remove later
    printf("thread %d created\n", thread_ctx->id);


    // this is literily thread safe since it's per-thread structure
    threads_stats t = malloc(sizeof(struct Threads_stats));
    t->id = thread_ctx->id; // Thread ID (placeholder)

    t->stat_req = 0; // Static request count
    t->dynm_req = 0; // Dynamic request count
    t->post_req = 0; // POST request count
    t->total_req = 0; // Total request count


    // not busy waiting but it means the thread is active
    while (1) {
        //  let's pop the first request, but make sure to lock
        pthread_mutex_lock(&requests_q_mutex);

        // is_empty must be called while holding the mutex.
        while (rqueue_is_empty(thread_ctx->rqueue)) {
            // if no requests are avilable then enter cond blocking
            pthread_cond_wait(&has_requests, &requests_q_mutex);
        }

        // don't check the lock I already have it (pthread_cond wakes holding the lock)
        int connfd = rqueue_pop(thread_ctx->rqueue);

        // don't hold the mutex while processing! shooting your self in the foot and
        // defices the purpose of the parallelism.
        pthread_mutex_unlock(&requests_q_mutex);


        // TODO: HW3 — Record the request arrival time here.
        time_stats dum;

        // gettimeofday(&arrival, NULL);

        // Call the request handler
        requestHandle(connfd, dum, t, thread_ctx->log);


        Close(connfd); // Close the connection
    }


    return NULL;
}

pthread_t threads[MAX_THREADS];

void init_threads_pool(server_log log, rqueue_p queue) {
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_t thread_id;


        thread_ctx_t *thread_ctx = malloc(sizeof(thread_ctx_t));

        thread_ctx->rqueue = queue;
        thread_ctx->log = log;
        thread_ctx->id = i + 1;


        // we need to create a thread and add it to the pool
        int status = pthread_create(&thread_id, NULL, processor_entry, thread_ctx);
        if (status == -1) {
            //TODO: make a centralized handler
            // exit on error policy
            exit(0);
        }

        // bookkeep
        threads[i] = thread_id;
    }
}


int main(int argc, char *argv[]) {
    // if weired print stuff happens it's because of fucking buffering
    // setbuf(stdout, NULL);


    // Create the global server log
    server_log log = create_log();

    rqueue_p rqueue = malloc(sizeof(rqueue_t));
    // init the queue
    init_rqueue(rqueue, &requests_q_mutex);


    // init threads with ctx
    init_threads_pool(log, rqueue);

    int port, clientlen;
    struct sockaddr_in clientaddr;

    getargs(&port, argc, argv);

    int listenfd = Open_listenfd(port);

    // this is not busy waiting rather the server is active
    while (1) {
        clientlen = sizeof(clientaddr);
        int connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);
        //connected accepted - enqueue it.

        // wait (thread blocking no busy wait) until the queue is not full, it internally uses sempahores

        // append is thread safe since it internally uses locks
        rqueue_append(rqueue, connfd);


        // then we tell everybody the state changed
        pthread_cond_signal(&has_requests);
    }

    // Clean up the server log before exiting
    destroy_log(log);

    // TODO: HW3 — Add cleanup code for the thread pool and queue.
}
