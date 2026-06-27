#include "segel.h"
#include "request.h"
#include "log.h"
#include "requests_queue.h"

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
void validate_args(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
}

// TODO: HW3 — Task 1: Initialize the thread pool and request queue.
// This server currently handles all requests in the main thread.

// TODO: HW3 — Task 4: Add the UDP channel (see the UDP_* wrappers in segel.c).

// TODO: HW3 — Extend getargs() to parse the full argument list.

pthread_cond_t has_requests = PTHREAD_COND_INITIALIZER;

pthread_mutex_t requests_q_mutex = PTHREAD_MUTEX_INITIALIZER;


pthread_cond_t has_udp_requests = PTHREAD_COND_INITIALIZER;
pthread_mutex_t udp_requests_q_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_ctx {
    int id; // thread id

    rqueue_t *udp_rqueue;
    pthread_mutex_t udp_queue_mutex;


    /* services */
    server_log log;
    rqueue_t *rqueue;
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

    int I_have_lock = 0;

    // not busy waiting but it means the thread is active
    while (1) {
        // I want to sleep on this lock
        pthread_mutex_lock(&requests_q_mutex);


        while (
            rqueue_is_empty(thread_ctx->rqueue) && rqueue_is_empty(thread_ctx->udp_rqueue)
        ) {
        label_sleep:
            // then wait until not empty
            pthread_cond_wait(&has_requests, &requests_q_mutex);
        }
        // otheriwse I have the lock lets process


        typedef struct udpNode {
            rqueue_request_data_t data;
            struct udpNode *next;
        } udpNode;

        udpNode udp_work;
        udp_work.next = NULL;


        udpNode *current_udp_node = &udp_work;
        // pull all the udp work
        while (!rqueue_is_empty(thread_ctx->udp_rqueue)) {
            udpNode *item = malloc(sizeof(udpNode));


            current_udp_node->next = item;

            item->data = rqueue_pop(thread_ctx->udp_rqueue);

            current_udp_node = item;
        }


        short has_tcp = 0;
        rqueue_request_data_t conndata;


        if (!rqueue_is_empty(thread_ctx->rqueue)) {
            // don't check the lock I already have it (pthread_cond wakes holding the lock)
            conndata = rqueue_pop(thread_ctx->rqueue);
            has_tcp = 1;
        }

        // then give up the lock
        pthread_mutex_unlock(&requests_q_mutex);


        // now process
        printf("thread %d processing\n", thread_ctx->id);


        current_udp_node = udp_work.next;
        while (current_udp_node != NULL) {
            //get the udp request

            rqueue_request_data_t udpdata = current_udp_node->data;

            char resp[1024];
            int resp_size = snprintf(resp, sizeof(resp),
                                     "response from thread %d\n",
                                     thread_ctx->id);


            UDP_Write(udpdata.connfd, &udpdata.clientaddr, resp, resp_size);

            current_udp_node = current_udp_node->next;
        }

        // then free
        current_udp_node = udp_work.next;
        while (current_udp_node != NULL) {
            udpNode *nextNode = current_udp_node->next;
            free(current_udp_node);
            current_udp_node = nextNode;
        }


        // if there is a tcp
        if (has_tcp) {
            time_stats dum;

            // the problem with doing this in the master thread is the vtime is scopped meaning it's life-cycle is tied to the master thread not this
            // thread, I could've replaced it with pointers instead but lets keep it as is.
            dum.task_arrival = conndata.arrival_time;
            dum.task_dispatch = the_time();

            // gettimeofday(&arrival, NULL);

            // Call the request handler
            requestHandle(conndata.connfd, dum, t, thread_ctx->log);


            Close(conndata.connfd); // Close the connection
        }
    }


    return NULL;
}


thread_ctx_t **init_threads_pool(server_log log, rqueue_p queue, int thread_count) {
    // array of thread ctxs.
    thread_ctx_t **threads = malloc(thread_count * sizeof(thread_ctx_t *));

    for (int i = 0; i < thread_count; i++) {
        pthread_t thread_id;


        thread_ctx_t *thread_ctx = malloc(sizeof(thread_ctx_t));

        // pthread_mutex_init(&thread_ctx->udp_queue_mutex, NULL);

        // instead of reinveing the wheel i'l use the same queue structure, just for each thread
        rqueue_p udp_rqueue = rqueue_create();

        // init the queue, and tie it to udp queue mutex, since this is udp queue, then make it's size unlimited.
        // rqueue_init(udp_rqueue, &thread_ctx->udp_queue_mutex, -1);
        rqueue_init(udp_rqueue, &requests_q_mutex, -1);
        thread_ctx->udp_rqueue = udp_rqueue;


        thread_ctx->rqueue = queue;
        thread_ctx->log = log;
        thread_ctx->id = i + 1;


        // we need to create a thread and add it to the pool
        int status = pthread_create(&thread_id, NULL, processor_entry, thread_ctx);
        if (status != 0) {
            //TODO: make a centralized handler
            // exit on error policy
            exit(0);
        }

        // bookkeep
        threads[i] = thread_ctx;
    }
    return threads;
}

#define MAX(x,y) (x < y ? y : x)

int main(int argc, char *argv[]) {
    // if weired print stuff happens it's because of fucking buffering
    // setbuf(stdout, NULL);

    validate_args(argc, argv);


    int tcp_port = atoi(argv[1]);
    int udp_port = atoi(argv[2]);
    int threads_count = atoi(argv[3]);
    int queue_size = atoi(argv[4]);
    int debug_sleep_time = atoi(argv[5]);

    // first let's make sure we can open tcp and udp
    int listenfd = Open_listenfd(tcp_port);

    int udpsd = UDP_Open(udp_port);


    // Create the global server log
    server_log log = create_log();

    rqueue_p rqueue = rqueue_create();
    // init the queue
    rqueue_init(rqueue, &requests_q_mutex, queue_size);


    // init threads with ctx
    thread_ctx_t **threads = init_threads_pool(log, rqueue, threads_count);

    struct sockaddr_in clientaddr;


    int clientlen;

    int maxFD = MAX(listenfd, udpsd) + 1;

    // this is not busy waiting rather the server is active
    while (1) {
        // create the fds set
        fd_set readfds;
        FD_ZERO(&readfds);

        // we want the tcp fd
        FD_SET(listenfd, &readfds);
        FD_SET(udpsd, &readfds);
        // or the udpfd


        // who is read first?
        int rc = select(
            maxFD,
            &readfds,
            NULL,
            NULL,
            NULL
        );

        if (rc < 0) {
            //TODO: handle error
            exit(0);
        }


        // since the priority is to udp we check first
        if (FD_ISSET(udpsd, &readfds)) {
            // then add it to the thread specfic
            // so first we get what id do they want?
            // since I have the max thread number, buff_len = log(n), but since I don't have the log function yet, i'll pass as is. since log(n) < n, although this is sloppy
            // TODO: change to log or smthing (maybe simple string len check)


            char buff[32];
            int n = UDP_Read(udpsd, &clientaddr, buff, sizeof(buff) - 1);
            if (n <= 0) {
                continue;
            }
            buff[n] = '\0';
            int threadId = atoi(buff);


            rqueue_request_data_t data = {
                .connfd = udpsd,
                // copy addr
                .clientaddr = clientaddr,
                // for completness
                .arrival_time = the_time(),
            };

            //TODO: validate thread id

            // the queue itself does aquire the lock
            rqueue_append(threads[threadId - 1]->udp_rqueue, data);
        }

        // if tcp is ready (it's math or not xor)
        if (FD_ISSET(listenfd, &readfds)) {
            clientlen = sizeof(clientaddr);
            int connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);
            //connected accepted - enqueue it.

            rqueue_request_data_t data = {
                .connfd = connfd,
                // here the time is when seen first not inserted (can be delayed due to locking or full)
                .arrival_time = the_time(),
            };

            // wait (thread blocking no busy wait) until the queue is not full, it internally uses sempahores
            // append is thread safe since it internally uses locks
            rqueue_append(rqueue, data);
        }

        // then we tell everybody the state changed
        pthread_cond_broadcast(&has_requests);
    }

    // Clean up the server log before exiting
    destroy_log(log);

    // TODO: HW3 — Add cleanup code for the thread pool and queue.
}
