//
// Created by jamal on 26/06/2026.
//
#include "requests_queue.h"

#include <stdlib.h>

#define QUEUE_LOCK pthread_mutex_lock(queue->mutex);
#define QUEUE_UNLOCK pthread_mutex_unlock(queue->mutex);


short rqueue_is_empty(rqueue_t *queue) {
    short result = (queue->head == NULL);

    return !!result;
}

void rqueue_pop(rqueue_t *queue, rqueue_request_data_t *data) {
    rqueue_request_t *head = queue->head;
    // this shouldn't be generaly the case
    // since threads have a cond var on is_queue_empty, but i'll keep it.
    if (head == NULL) {
        return;
    }

    data->arrival_time = head->data.arrival_time;
    data->connfd = head->data.connfd;

    // remove from list
    queue->head = head->next;

    // if head is null now, it means we actually popped the last, so
    if (queue->head == NULL) queue->last = NULL;

    // then free
    free(head);


    // now after we took a slot out, we freed one of the virtual splots so we do
    sem_post(&queue->capacity_sem);
}

void init_rqueue(rqueue_t *queue, pthread_mutex_t *mutex) {
    queue->head = NULL;
    queue->last = NULL;
    queue->mutex = mutex;

    int queue_capacity = 5;

    sem_init(&queue->capacity_sem, 0, queue_capacity);
}

// temp on the condition mutex is achieved.
// later mutex is moved here....
void rqueue_append(rqueue_t *queue, rqueue_request_data_t data) {
    // can we aquire a virtual slot ? if so then insert other wise wait for some body to free.
    sem_wait(&queue->capacity_sem);

    // then aquire the readers mutex
    QUEUE_LOCK


    //TODO: handle failing mallocs.
    rqueue_request_t *request = malloc(sizeof(rqueue_request_t));

    request->data.arrival_time = data.arrival_time;
    request->data.connfd = data.connfd;


    request->next = NULL;

    rqueue_request_t *current_last = queue->last;
    queue->last = request;

    // if no last then no first
    if (current_last == NULL) {
        queue->head = request;
    } else {
        // also last cannot have a next
        current_last->next = request;
    }


    QUEUE_UNLOCK
}
