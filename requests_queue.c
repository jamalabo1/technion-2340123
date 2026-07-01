//
// Created by jamal on 26/06/2026.
//

#include "requests_queue.h"
#include "segel.h"

#define QUEUE_LOCK Pthread_mutex_lock(queue->mutex);
#define QUEUE_UNLOCK Pthread_mutex_unlock(queue->mutex);

struct RequestsQueue {
    rqueue_request_t *head;
    rqueue_request_t *last;

    pthread_mutex_t *mutex;
    sem_t capacity_sem;

    // is bounded (udp or tcp queue)?
    short is_bounded;
};

short rqueue_is_empty(rqueue_t *queue) {
    short result = (queue->head == NULL);

    return !!result;
}

rqueue_p rqueue_create() {
    return Malloc(sizeof(rqueue_t));
}

void rqueue_init(rqueue_t *queue, pthread_mutex_t *mutex, int queue_capacity) {
    queue->head = NULL;
    queue->last = NULL;
    queue->mutex = mutex;

    if (queue_capacity > 0) {
        Sem_init(&queue->capacity_sem, 0, queue_capacity);
        queue->is_bounded = 1;
    } else {
        queue->is_bounded = 0;
    }
}


rqueue_request_data_t rqueue_pop(rqueue_t *queue) {
    rqueue_request_t *head = queue->head;
    // this shouldn't be the case
    // since threads have a cond var on is_queue_empty
    // if (head == NULL) {
    //     return;
    // }

    // copy it, I mean why not?
    rqueue_request_data_t data = head->data;

    // remove from list
    queue->head = head->next;

    // if head is null now, it means we actually popped the last, so
    if (queue->head == NULL) queue->last = NULL;

    // then free
    free(head);

    return data;
}

// this is saying I want to return a slot.
// I took an element and I want to return the virtual slot. this enforces pending + current <= size
void commit(rqueue_t* queue) {
    if (queue->is_bounded) {
        // now after we took a slot out, we freed one of the virtual slots so we do
        Sem_post(&queue->capacity_sem);
    }
}

short rqueue_is_full_safe(rqueue_t *queue) {
    int val;

    Sem_getvalue(&queue->capacity_sem, &val);

    // if the value is 0, then it's full, otherwise not.
    return val == 0;
}


// temp on the condition mutex is achieved.
// later mutex is moved here....
void rqueue_append_safe(rqueue_t *queue, rqueue_request_data_t data) {
    if (queue->is_bounded) {
        // can we aquire a virtual slot ? if so then insert otherwise wait for somebody to free.
        Sem_wait(&queue->capacity_sem);
    }

    // then acquire the readers mutex
    QUEUE_LOCK


    rqueue_request_t *request = Malloc(sizeof(rqueue_request_t));

    // already copied
    request->data = data;
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
