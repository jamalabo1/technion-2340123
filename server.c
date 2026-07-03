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

pthread_mutex_t requests_q_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t has_requests = PTHREAD_COND_INITIALIZER;

struct thread_ctx {
    int id; // thread id

    // udp queue
    rqueue_t *udp_rqueue;

    /* services */
    server_log log;
    rqueue_t *rqueue;

    // signal
    int signal_fd;
};

typedef struct thread_ctx thread_ctx_t;


void _thread_handle_udp(rqueue_request_data_t *data, threads_stats thread_stats) {
    // format response
    char buf[MAXLINE];
    buf[0] = '\0'; // start with 0

    // use the already there function
    int resp_size = append_thread_log(buf, thread_stats);

    // send response
    UDP_Write(data->connfd, &data->clientaddr, buf, resp_size);
}


void *processor_entry(void *thread_ctx_p) {
    thread_ctx_t *thread_ctx = thread_ctx_p;

    // this is literally thread safe since it's per-thread structure
    threads_stats t = Malloc(sizeof(struct Threads_stats));
    t->id = thread_ctx->id; // Thread ID (placeholder)

    t->stat_req = 0; // Static request count
    t->dynm_req = 0; // Dynamic request count
    t->post_req = 0; // POST request count
    t->total_req = 0; // Total request count

    // not busy waiting but it means the thread is active
    while (1) {
        // attempt to acquire the lock
        Pthread_mutex_lock(&requests_q_mutex);

        // IMPORTANT: both queues depend on this lock, so it's safe to check both on it
        while (
            rqueue_is_empty(thread_ctx->rqueue) &&
            rqueue_is_empty(thread_ctx->udp_rqueue)
        ) {
            // then wait until not empty
            Pthread_cond_wait(&has_requests, &requests_q_mutex);
        }

        // otheriwse I have the lock lets process

        typedef struct udpNode {
            rqueue_request_data_t data;
            struct udpNode *next;
        } udpNode;

        // dummy head
        udpNode udp_work;
        udp_work.next = NULL;

        udpNode *current_udp_node = &udp_work;
        // pull all the udp work
        while (!rqueue_is_empty(thread_ctx->udp_rqueue)) {
            udpNode *item = Malloc(sizeof(udpNode));
            current_udp_node->next = item;


            // copy get the request data
            item->data = rqueue_pop(thread_ctx->udp_rqueue);
            item->next = NULL;

            current_udp_node = item;
        }


        // is there any tcp work?
        short has_tcp = 0;
        rqueue_request_data_t conndata;
        time_stats dum;


        if (!rqueue_is_empty(thread_ctx->rqueue)) {
            // don't check the lock I already have it (pthread_cond wakes holding the lock)
            conndata = rqueue_pop(thread_ctx->rqueue);
            has_tcp = 1;

            // record them before processing udp to literally make on queue lift.
            dum.task_arrival = conndata.arrival_time;
            dum.task_dispatch = the_time();
        }

        // then give up the lock
        Pthread_mutex_unlock(&requests_q_mutex);

        // now process
        // printf("thread %d processing\n", thread_ctx->id);

        // skip the dummy and process udp
        current_udp_node = udp_work.next;
        while (current_udp_node != NULL) {
            //get the udp request
            rqueue_request_data_t udpdata = current_udp_node->data;

            // handle the udp
            _thread_handle_udp(&udpdata, t);

            current_udp_node = current_udp_node->next;
        }

        // then free to avoid leaks
        current_udp_node = udp_work.next;
        while (current_udp_node != NULL) {
            udpNode *nextNode = current_udp_node->next;
            free(current_udp_node);
            current_udp_node = nextNode;
        }

        // if there is a tcp
        if (has_tcp) {
            // Call the request handler
            requestHandle(conndata.connfd, dum, t, thread_ctx->log);

            Close(conndata.connfd); // Close the connection

            // and when I finish i'll return the virtual slot
            int available_space = commit(thread_ctx->rqueue);

            // if was full, I finished proccessing something, so let the main thread know
            // and to avoid needing to read all the signals from threads, i'll only let the one who freed some space
            // send a signal, so I only need to read 1 signal in the main thread
            if (available_space == 1) {
                // having available space = 1 at the point, means it was 0 before commit
                // so the main thread was blocked for sure, even if capacity_size = 1, so it's ok

                char signal = 1;
                Write(thread_ctx->signal_fd, &signal, 1);
            }
        }
    }


    return NULL;
}


thread_ctx_t **init_threads_pool(server_log log, rqueue_p queue, int thread_count, int signal_fd) {
    // array of thread ctxs.
    thread_ctx_t **threads = Malloc(thread_count * sizeof(thread_ctx_t *));

    for (int i = 0; i < thread_count; i++) {
        // create a ctx
        thread_ctx_t *thread_ctx = Malloc(sizeof(thread_ctx_t));

        // instead of reinventing the wheel i'l use the same queue structure, just for each thread
        rqueue_p udp_rqueue = rqueue_create();

        // 'll tie the queue to the global mutex for simplicity
        rqueue_init(udp_rqueue, &requests_q_mutex, -1);
        thread_ctx->udp_rqueue = udp_rqueue;

        // set the ctx fields
        thread_ctx->id = i + 1;
        thread_ctx->rqueue = queue;
        thread_ctx->log = log;
        thread_ctx->signal_fd = signal_fd;

        // we need to create a thread and add it to the pool
        pthread_t thread_id;
        Pthread_create(&thread_id, NULL, processor_entry, thread_ctx);

        // bookkeep
        threads[i] = thread_ctx;
    }
    return threads;
}

int max(int a, int b) {
    return (a > b) ? a : b;
}



int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s [tcp_portnum] [udp_portnum] [threads] [queue_size] [debug_sleep_time]\n", argv[0]);
        exit(1);
    }

    int tcp_port = parse_int(argv[1]);
    int udp_port = parse_int(argv[2]);
    int threads_count = parse_int(argv[3]);
    int queue_size = parse_int(argv[4]);
    double debug_sleep_time = parse_float(argv[5]);

    // according to piazza @254 1024 is included
#define MIN_PORT 1024
#define MAX_PORT 65535

    if (tcp_port < MIN_PORT || tcp_port > MAX_PORT) {
        app_error("tcp port must valid in the range 1024-65535");
    }
    if (udp_port < MIN_PORT || udp_port > MAX_PORT) {
        app_error("udp port must valid in the range 1024-65535");
    }

    if (tcp_port == udp_port) {
        app_error("tcp port must not equal udp_port");
    }
    if (threads_count <= 0) {
        app_error("threads_count must be positive");
    }
    if (queue_size <= 0) {
        app_error("queue_size must be positive");
    }


    // first let's make sure we can open tcp and udp
    int listenfd = Open_listenfd(tcp_port);
    int udpsd = UDP_Open(udp_port);

    // Create the global server log
    server_log log = create_log(debug_sleep_time);

    // create the queue
    rqueue_p rqueue = rqueue_create();
    // init the queue
    rqueue_init(rqueue, &requests_q_mutex, queue_size);


    // wake signal pipe, to wake the server when a thread finishes processing from a full queue
    // this is better than timeout because it's async and i think literally event driven
    int cap_pipe[2];
    Pipe(cap_pipe);

    // init threads with ctx
    thread_ctx_t **threads = init_threads_pool(log, rqueue, threads_count, cap_pipe[1]);


    struct sockaddr_in clientaddr;
    int clientlen;

    // main thread reads, workers write
    int signal_fd = cap_pipe[0];


    // we also need to listen to what threads have to say
    int maxFD = max(max(listenfd, udpsd), signal_fd) + 1;

    // this is not busy waiting rather the server is active
    while (1) {
        // create the fds set
        fd_set readfds;
        FD_ZERO(&readfds);

        // if the tcp queue is full, then dont ask the kernel for tcp requests
        // since it might block us when getting to the queue append
        if (!rqueue_is_full_safe(rqueue)) {
            // we want the tcp fd
            FD_SET(listenfd, &readfds);
        }

        // or the udpfd
        FD_SET(udpsd, &readfds);

        // and the capacity signal, no need to do any processing it's just to free the select
        FD_SET(signal_fd, &readfds);

        // who to read first?
        Select(
            maxFD,
            &readfds,
            NULL,
            NULL,
            NULL
        );

        if (FD_ISSET(signal_fd, &readfds)) {
            char dummy;
            Read(signal_fd, &dummy, 1);
        }

        // since the priority is to udp we check first
        if (FD_ISSET(udpsd, &readfds)) {
            // then add it to the thread specfic
            // so first we get what id do they want?
            // since I have the max thread number, buff_len = log(n), but since I don't have the log function yet, i'll pass as is. since log(n) < n, although this is sloppy
            // TODO: change to log or smthing (maybe simple string len check)

            // what's the digit number of 2^32? it's log(2^32) = 32. so if we assume it's an int, it's of length at most 32.
            char buff[32];
            int n = UDP_Read(udpsd, &clientaddr, buff, sizeof(buff) - 1);
            if (n <= 0) {
                // no data
                continue;
            }
            buff[n] = '\0';
            // parse the id
            int threadId;
            int valid_thread_id = try_parse_int(buff, &threadId);

            if (!valid_thread_id || threadId > threads_count || threadId <= 0) {
                // it does not exist, udp skips invalids, but keep processing to reach tcp
            } else {
                // set the data
                rqueue_request_data_t data = {
                    .connfd = udpsd,
                    // copy addr
                    .clientaddr = clientaddr,
                    // for completeness
                    .arrival_time = the_time(),
                };


                thread_ctx_t *thread_ctx = threads[threadId - 1];

                // the queue itself does acquire the lock
                rqueue_append_safe(thread_ctx->udp_rqueue, data);
            }
        }

        // if tcp is ready (it's math OR not XOR)
        if (FD_ISSET(listenfd, &readfds)) {
            clientlen = sizeof(clientaddr);
            int connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *) &clientlen);
            //connected accepted - enqueue it.

            rqueue_request_data_t data = {
                .connfd = connfd,
                // here the time is when seen first not inserted (can be delayed due to locking or full)
                .arrival_time = the_time(),

                // this is just for completeness, although not used on the tcp path.
                .clientaddr = clientaddr,
            };

            // wait (thread blocking no busy wait) until the queue is not full, it internally uses semaphores
            // append is thread safe since it internally uses locks
            rqueue_append_safe(rqueue, data);
        }

        // then we tell everybody the state changed
        Pthread_cond_broadcast(&has_requests);
    }

    // Clean up the server log before exiting
    destroy_log(log);
}
