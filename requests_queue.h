//
// Created by jamal on 26/06/2026.
//

#ifndef REQUESTS_QUEUE_H
#define REQUESTS_QUEUE_H

#include <pthread.h>
#include <semaphore.h>

struct Request {
    int connfd;

    struct Request *next;
};

typedef struct Request rqueue_request_t;


struct RequestsQueue {
    rqueue_request_t *head;
    rqueue_request_t *last;

    pthread_mutex_t *mutex;
    sem_t capacity_sem;
};

typedef struct RequestsQueue rqueue_t;
typedef rqueue_t* rqueue_p;

void init_rqueue(rqueue_t *queue, pthread_mutex_t *mutex);

short rqueue_is_empty(rqueue_t *queue);

int rqueue_pop(rqueue_t *queue);

void rqueue_append(rqueue_t *queue, int connfd);

#endif //REQUESTS_QUEUE_H
