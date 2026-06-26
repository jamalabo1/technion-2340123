//
// Created by jamal on 26/06/2026.
//

#ifndef REQUESTS_QUEUE_H
#define REQUESTS_QUEUE_H

struct Request {
    int connfd;

    struct Request *next;
};

typedef struct Request rqueue_request_t;


struct RequestsQueue {
    rqueue_request_t *head;

    rqueue_request_t* last;
};

typedef struct RequestsQueue rqueue_t;


 short is_queue_empty(rqueue_t *queue) {
    if (queue->head == NULL) return 1;
    return 0;
}

 int rqueue_pop(rqueue_t *queue) {
    rqueue_request_t *head = queue->head;
    if (head == NULL) return -1;


    int connfd = head->connfd;

    // remove from list
    queue->head = head->next;

    // if head is null now, it means we actually popped the last, so
    if (queue->head == NULL) queue->last = NULL;

    // then free
    free(head);

    return connfd;
}

void init_rqueue(rqueue_t *queue) {
    queue->head = NULL;
}

// temp on the condition mutex is achieved.
// later mutex is moved here....
void rqueue_append(rqueue_t *queue, int connfd) {
    rqueue_request_t *request = malloc(sizeof(rqueue_request_t));
    request->connfd = connfd;
    request->next = NULL;

    rqueue_request_t* current_last = queue->last;
    queue->last = request;

    // if no last then no first
    if (current_last == NULL) {
        queue->head = request;
    } else {
        // also last cannot have a next
        current_last->next = request;
    }
}

#endif //REQUESTS_QUEUE_H
