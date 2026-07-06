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
    pthread_cond_t* cond_var;

    sem_t capacity_sem;

    // is bounded (udp or tcp queue)?
    short is_bounded;

    // for atomic operations along side the mutex
    volatile int available_slots;
};

short rqueue_is_empty(rqueue_t *queue) {
    short result = (queue->head == NULL);

    return !!result;
}

rqueue_p rqueue_create() {
    return Malloc(sizeof(rqueue_t));
}

void rqueue_init(rqueue_t *queue, pthread_mutex_t *mutex, pthread_cond_t* cond_var, int queue_capacity) {
    queue->head = NULL;
    queue->last = NULL;
    queue->mutex = mutex;
    queue->cond_var = cond_var;

    if (queue_capacity > 0) {
        Sem_init(&queue->capacity_sem, 0, queue_capacity);
        queue->is_bounded = 1;
        queue->available_slots = queue_capacity;
    } else {
        queue->is_bounded = 0;

        // infinite slots, I can keep this uninitialized since its never used in the non bound lane
        queue->available_slots = -1;
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
int commit(rqueue_t* queue) {
    if (queue->is_bounded) {
        // now after we took a slot out, we freed one of the virtual slots so we do
        Sem_post(&queue->capacity_sem);
        // ok so the problem in reading the sem value is that its not atomic with post, so it might be that
        // thread A commit, ctx switch, thread B commit, B read -> 2, ctx A, A read -> 2. so we never reach 1.

        // returns the newly available space, this is doing the work twice (adds 1 twice),
        // so I guess there must be a better way to do it (like fetch add new or smthng but i'll keep as is)
        return atomic_add(&queue->available_slots, 1) + 1;
    }

    // undefined behaviour to commit a non bounded queue. (no one does it in the code)
    return -1;
}

short rqueue_is_full_safe(rqueue_t *queue) {

    // if the value is 0, then it's full, otherwise not.
    return queue->available_slots == 0;
}


// temp on the condition mutex is achieved.
// later mutex is moved here....
void rqueue_append_safe(rqueue_t *queue, rqueue_request_data_t data) {
    if (queue->is_bounded) {
        // can we acquire a virtual slot ? if so then insert otherwise wait for somebody to free.
        Sem_wait(&queue->capacity_sem);

        // you might ask, why use both semaphores and atomics? sems block wait while atomics dont.
        atomic_dec(&queue->available_slots);
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


    // then we tell everybody the state changed. why in append not global in server?
    // because suppose this: server wants to handle 2 requests, udp and tcp,
    // if the broadcast was global (after it added both), then if the udp'ed thread A wakes up first, it will handle it's udp and the tcp
    // but if we had 2 threads in the system, and the udp takes time, then we blocked processing of the tcp for no reason.
    // if we did 2 phase broadcast, then we create the posibility for:
    // 1. thread A waking before adding the tcp request.
    // 2. thread B waking after adding the tcp and before thread A takes the tcp
    // so it can be the case that we broadcast to threads all go to sleep except thread A, it processes it udp
    // and while doing so we wake up the rest of the threads to handle the tcp.
    Pthread_cond_broadcast(queue->cond_var);

    QUEUE_UNLOCK
}
