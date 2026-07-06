//
// Created by jamal on 26/06/2026.
//

#ifndef REQUESTS_QUEUE_H
#define REQUESTS_QUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <sys/time.h>

struct RequestData {
    int connfd;
    struct sockaddr_in clientaddr;
    struct timeval arrival_time;
};

typedef struct RequestData rqueue_request_data_t;

struct Request {
    rqueue_request_data_t data;
    struct Request *next;
};

typedef struct Request rqueue_request_t;


struct RequestsQueue;

typedef struct RequestsQueue rqueue_t;
typedef rqueue_t *rqueue_p;

rqueue_p rqueue_create();

void rqueue_init(rqueue_t *queue, pthread_mutex_t *mutex, pthread_cond_t* cond_var, int queue_size);

short rqueue_is_empty(rqueue_t *queue);

rqueue_request_data_t rqueue_pop(rqueue_t *queue);

int commit(rqueue_t* queue);

void rqueue_append_safe(rqueue_t *queue, rqueue_request_data_t arrival_time);

short rqueue_is_full_safe(rqueue_t *queue);

#endif //REQUESTS_QUEUE_H
