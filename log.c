#include <stdlib.h>
#include <string.h>
#include "log.h"

#include <pthread.h>
#include <semaphore.h>

#include "segel.h"

#define LOCK(mutex) pthread_mutex_lock(&log->mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(&log->mutex)

typedef struct Log_entry *log_entry;

struct Log_entry {
    char *data;
    int len;

    log_entry next;
};


// Opaque struct definition
struct Server_Log {
    // TODO: Implement internal log storage (e.g., dynamic buffer, linked list, etc.)
    int readers_count; // how many readers
    int writers_count; // how many writers

    pthread_mutex_t rc_mutex; // reader count mutex
    pthread_mutex_t wc_mutex; // writer count mutex

    // is the gate open?  readers cannot enter if it's closed (writers_count > 0).
    pthread_cond_t gate_cond;


    // the actual log mutex
    sem_t log_sem;

    // lock free (atomic) writer is there waiting
    int writer_arrived;


    log_entry head;
    log_entry last;
    long entries_length;
};

// Creates a new server log instance (stub)
server_log create_log() {
    server_log log_inst = (server_log) malloc(sizeof(struct Server_Log));
    log_inst->readers_count = 0;
    log_inst->writers_count = 0;
    log_inst->writer_arrived = 0;

    // int's mutex
    pthread_mutex_init(&log_inst->rc_mutex, NULL);
    pthread_mutex_init(&log_inst->wc_mutex, NULL);

    // gate cond
    pthread_cond_init(&log_inst->gate_cond, NULL);

    // log sempahore
    sem_init(&log_inst->log_sem, 0, 1);


    // dummy empty head
    log_inst->head = malloc(sizeof(log_entry));
    log_inst->head->len = 0;
    log_inst->head->next = NULL;
    log_inst->last = log_inst->head;

    log_inst->entries_length = 0;

    return log_inst;
}

// Destroys and frees the log (stub)
void destroy_log(server_log log) {
    // TODO: Free all internal resources used by the log
    sem_destroy(&log->log_sem);

    pthread_mutex_destroy(&log->rc_mutex);
    pthread_mutex_destroy(&log->wc_mutex);
    pthread_cond_destroy(&log->gate_cond);

    free(log);
}


int _protected_read(server_log log, char * *dst) {
    // ok we are given a ref to the buffer var
    // at that place, we need to create the buffer
    char *data_ptr = (char *) malloc(log->entries_length * sizeof(char));
    if (data_ptr == NULL) {
        // problem
        return -1;
    }

    log_entry current = log->head;

    int i = 0;

    while (current != NULL) {
        // after skipping i bytes, write data of length len
        memcpy(data_ptr + i, current->data, current->len);

        // now skip len bytes
        i += current->len;

        current = current->next;
    }

    // now give them the place were the data lives
    *dst = data_ptr;

    return log->entries_length;
}

void _protected_write(server_log log, const char *data, int data_len) {
    log_entry entry = (log_entry) malloc(sizeof(log_entry));

    entry->data = (char *) malloc(data_len * sizeof(char));
    // copy the data
    strcpy(entry->data, data);


    entry->len = data_len;

    // update last's next
    log->last->next = entry;
    // we are the new last
    log->last = entry;

    log->entries_length += data_len;
}


// Returns dummy log content as string (stub)
int get_log(server_log log, char **dst) {
    // TODO: Return the full contents of the log as a dynamically allocated string
    // This function should handle concurrent access

    // if you come before the writer, I will not preempt you. but you need to let the writer know you reading:
    // but to allow multiple readers, we only hold the writer if the room is empty

    LOCK(wc_mutex);
    // I cannot enter, so I need a cond lock
    // if the gate is closed, I cannot enter it must be open
    while (
        log->writers_count > 0 ||

        // this is saying even though os gave me the lock, i'll still give it up if there is any writer waiting.
        log->writer_arrived > 0
        // when is this a problem?
        // if I read the value as 1 while the broadcast happens before
        // this cannot be the case because writers need wc_mutex to broadcast, which I currently have so I'm safe.
    ) {
        pthread_cond_wait(&log->gate_cond, &log->wc_mutex);
    }

    // if nobody is writing, then I can proceed. but first let me aquire the log lock
    LOCK(rc_mutex);
    log->readers_count++;
    if (log->readers_count == 1) {
        // if i'm the first reader then lock the log
        sem_wait(&log->log_sem);
    }
    UNLOCK(rc_mutex);

    UNLOCK(wc_mutex);
    // so the story goes:
    // the os gives the reader wc_mutex, if no writer has registered it self, we are assume to continue. what else can we do?
    // do we order the locks such that writer always tries mutexA first and reader tries mutexB first? this can create a deadlock
    // ok now, reader that finishes the while cond assumes no writer is there, so it aquires the log_sem and starts reading
    // through fariness the writer time comes eventually, and it registers it self, therefore subsequent readers get blocked.
    // and the sem-wait(log_sem) will get rotated between the writers only since no reader can wait for it (they are blocked in the writers_cond)

    int len = _protected_read(log, dst);


    LOCK(rc_mutex);
    log->readers_count--;

    if (log->readers_count == 0) {
        // if i'm the last to leave then turn off the lights
        // it can be the i'm the last but I was not the first so this is why I need semaphores
        sem_post(&log->log_sem);
    }

    UNLOCK(rc_mutex);


    return len;
}


// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char *data, int data_len) {
    // TODO: Append the provided data to the log
    // This function should handle concurrent access

    // atomic increase to break the mutex fariness
    atomic_inc(&log->writer_arrived);

    LOCK(wc_mutex);
    // i'm waiting...
    log->writers_count++;
    // it's ok new readers will go to sleep anyway
    UNLOCK(wc_mutex);


    // now wait for the already there readers to finish and block other writers.
    sem_wait(&log->log_sem);

    _protected_write(log, data, data_len);

    sem_post(&log->log_sem);

    // atomic decrease do it before wc_mutex to have writer_arrived <= writers_count after lock
    atomic_dec(&log->writer_arrived);


    LOCK(wc_mutex);

    log->writers_count--;
    // if no more writers let's notify the readers
    if (log->writers_count == 0) {
        // open the gate for everybody, let the mutex queue block them
        pthread_cond_broadcast(&log->gate_cond);
    }

    UNLOCK(wc_mutex);
}
