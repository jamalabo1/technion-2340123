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
};

typedef struct thread_ctx thread_ctx_t;


void _thread_handle_udp(rqueue_request_data_t *data, threads_stats *thread_stats) {
    // format response
    char buf[MAXLINE];
    buf[0] = '\0'; // start with 0

    // use the already there function
    int resp_size = append_thread_log(buf, *thread_stats);

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

        if (!rqueue_is_empty(thread_ctx->rqueue)) {
            // don't check the lock I already have it (pthread_cond wakes holding the lock)
            conndata = rqueue_pop(thread_ctx->rqueue);
            has_tcp = 1;
        }

        // then give up the lock
        Pthread_mutex_unlock(&requests_q_mutex);

        // now process
        // printf("thread %d processing\n", thread_ctx->id);


        // skip the dummy
        current_udp_node = udp_work.next;
        while (current_udp_node != NULL) {
            //get the udp request
            rqueue_request_data_t udpdata = current_udp_node->data;

            // handle the udp
            _thread_handle_udp(&udpdata, &t);

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
            time_stats dum;

            // the problem with doing this in the master thread  vtime is scopped meaning it's life-cycle is tied to the master thread not this
            // thread. I could've replaced it with pointers instead but lets keep it as is.
            dum.task_arrival = conndata.arrival_time;
            dum.task_dispatch = the_time();

            // Call the request handler
            requestHandle(conndata.connfd, dum, t, thread_ctx->log);

            Close(conndata.connfd); // Close the connection

            // and when I finish i'll return the virtual slot
            commit(thread_ctx->rqueue);
        }



    }


    return NULL;
}


thread_ctx_t **init_threads_pool(server_log log, rqueue_p queue, int thread_count) {
    // array of thread ctxs.
    thread_ctx_t **threads = Malloc(thread_count * sizeof(thread_ctx_t *));

    for (int i = 0; i < thread_count; i++) {
        // create a ctx
        thread_ctx_t *thread_ctx = Malloc(sizeof(thread_ctx_t));

        // instead of reinveing the wheel i'l use the same queue structure, just for each thread
        rqueue_p udp_rqueue = rqueue_create();

        // 'll tie the queue to the global mutex for simplicity
        rqueue_init(udp_rqueue, &requests_q_mutex, -1);
        thread_ctx->udp_rqueue = udp_rqueue;

        // set the ctx fields
        thread_ctx->rqueue = queue;
        thread_ctx->log = log;
        thread_ctx->id = i + 1;

        // we need to create a thread and add it to the pool
        pthread_t thread_id;
        Pthread_create(&thread_id, NULL, processor_entry, thread_ctx);

        // bookkeep
        threads[i] = thread_ctx;
    }
    return threads;
}



int main(int argc, char *argv[]) {
    // if weired print stuff happens it's because of fucking buffering
    // setbuf(stdout, NULL);

    if (argc < 6) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int tcp_port = atoi(argv[1]);
    int udp_port = atoi(argv[2]);
    int threads_count = atoi(argv[3]);
    int queue_size = atoi(argv[4]);
    double debug_sleep_time = atof(argv[5]);

    if (tcp_port <= 1024) {
        app_error("tcp port must be above 1024");
    }
    if (udp_port <= 1024) {
        app_error("udp port must be above 1024");
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


    // init threads with ctx
    thread_ctx_t **threads = init_threads_pool(log, rqueue, threads_count);


    struct sockaddr_in clientaddr;
    int clientlen;

    int maxFD = ((listenfd > udpsd) ? listenfd : udpsd) + 1;

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


        FD_SET(udpsd, &readfds);
        // or the udpfd


        // who to read first?
        Select(
            maxFD,
            &readfds,
            NULL,
            NULL,
            NULL
        );

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
            int threadId = atoi(buff);

            // set the data
            rqueue_request_data_t data = {
                .connfd = udpsd,
                // copy addr
                .clientaddr = clientaddr,
                // for completness
                .arrival_time = the_time(),
            };

            if (threadId > threads_count || threadId <= 0) {
                // it does not exist
                app_error("requested thread does not exist");
            }

            thread_ctx_t *thread_ctx = threads[threadId - 1];

            // the queue itself does aquire the lock
            rqueue_append_safe(thread_ctx->udp_rqueue, data);
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

                // this is just for completness, although not used on the tcp path.
                .clientaddr = clientaddr,
            };

            // wait (thread blocking no busy wait) until the queue is not full, it internally uses sempahores
            // append is thread safe since it internally uses locks
            rqueue_append_safe(rqueue, data);
        }

        // then we tell everybody the state changed
        Pthread_cond_broadcast(&has_requests);
    }

    // Clean up the server log before exiting
    destroy_log(log);
}