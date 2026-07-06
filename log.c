#include <stdlib.h>
#include <string.h>
#include "log.h"

#include <pthread.h>
#include <semaphore.h>

#include "formatter.h"
#include "segel.h"

#define LOCK(mutex) Pthread_mutex_lock(&log->mutex)
#define UNLOCK(mutex) Pthread_mutex_unlock(&log->mutex)

typedef struct Log_entry *log_entry_p;
typedef struct Log_entry log_entry;

struct Log_entry {
    char *data;
    int len;

    log_entry_p next;
};


// Opaque struct definition
struct Server_Log {
    int readers_count; // how many readers
    int writers_count; // how many writers

    // pthread_mutex_t rc_mutex; // reader count mutex
    // pthread_mutex_t wc_mutex; // writer count mutex

    // since readers and writers will fight for wc_mutex anyway, so why not make it a single mutex?
    // if the reasoning is that writers dont wait for rc_mutex on exit, this is flawed because if N readers are already inside
    // you'd need to wait for all of them to exit to aquire the log_sem anyway.
    pthread_mutex_t c_mutex; // counters mutex

    // is the gate open?  readers cannot enter if it's closed (writers_count > 0).
    pthread_cond_t gate_cond;


    // the actual log "mutex". it must be sempahore so that it does not belong to any thread (ownership).
    // because the first reader to enter is not nessearily the last to exit.
    sem_t log_sem;

    // lock free (atomic) writer is there waiting
    volatile int writer_arrived;


    //TODO: use a better data structure! this scales poorly: O(N) reads
    log_entry_p head;
    log_entry_p last;
    long entries_length;

    double debug_sleep_time;

    // writer dumper
    char dumper[MAXLINE];
};

// Creates a new server log instance (stub)
server_log create_log(double debug_sleep_time) {
    server_log log_inst = (server_log) Malloc(sizeof(struct Server_Log));
    log_inst->readers_count = 0;
    log_inst->writers_count = 0;
    log_inst->writer_arrived = 0;

    // int's mutex
    // Pthread_mutex_init(&log_inst->rc_mutex);
    // Pthread_mutex_init(&log_inst->wc_mutex);
    Pthread_mutex_init(&log_inst->c_mutex);

    // gate cond
    Pthread_cond_init(&log_inst->gate_cond);

    // log sempahore
    Sem_init(&log_inst->log_sem, 0, 1);


    // dummy empty head
    log_inst->head = Malloc(sizeof(log_entry));
    log_inst->head->len = -1;
    log_inst->head->next = NULL;
    log_inst->head->data = NULL;

    log_inst->last = log_inst->head;
    log_inst->entries_length = 0;

    log_inst->debug_sleep_time = debug_sleep_time;

    // start dumper empty
    log_inst->dumper[0] = '\0';

    return log_inst;
}

// Destroys and frees the log
void destroy_log(server_log log) {
    Sem_destroy(&log->log_sem);

    // Pthread_mutex_destroy(&log->rc_mutex);
    // Pthread_mutex_destroy(&log->wc_mutex);
    Pthread_mutex_destroy(&log->c_mutex);


    Pthread_cond_destroy(&log->gate_cond);

    log_entry_p head = log->head;
    while (head != NULL) {
        log_entry_p next = head->next;
        free(head->data);
        free(head);
        head = next;
    }

    free(log);
}


long _protected_read(server_log log, char * *dst) {
    // ok we are given a ref to the buffer var
    // at that place, we need to create the buffer
    char *data_ptr = (char *) Malloc((log->entries_length + 1) * sizeof(char));

    int i = 0;
    log_entry_p current = log->head;

    while (current != NULL) {
        // dummy can have len = -1, and skip those (which idk how it could happen), empty lens
        // but im doing the if anyway so let's add it
        if (current->len <= 0) {
            // skip
            current = current->next;
            continue;
        }
        // after skipping i bytes, write data of length len
        memcpy(data_ptr + i, current->data, current->len);

        // len is strlen
        i += current->len;
        current = current->next;
    }

    // then null-terminate
    data_ptr[i] = '\0';

    // now give them the place were the data lives
    *dst = data_ptr;

    return log->entries_length;
}

void _protected_write(server_log log, time_stats *tm_stats, threads_stats t_stats) {
    log_entry_p entry = Malloc(sizeof(log_entry));

    // manually counted the stats size:
    // the longest is Stat-Thread-Dynamic:: with 21 bytes, and each number is int so it's bound by 10 bytes
    // therefore, we at max have 5*(21 + 1 + 11 + 2) = 175
    // so let's take 200 for bad luck

    // or another "more correct" solution, let's make it "dynamic", i'll allocate a log, "dumper"
    // write there i'll check the length, then create a dynamic one

    // 'reset'
    log->dumper[0] = '\0';

    // if was the first don't add #
    if (log->head != log->last) {
        // because having # means left is old and right is new
        // it either add to new's right or old's left
        // but olds left is implicitly assuming I know a right will come, which is impossible
        // so i need to do this when NEW entries come
        sprintf(log->dumper, "#");
    }

    // use already there function
    int data_len = append_stats(log->dumper, t_stats, *tm_stats);


    entry->data = Malloc((data_len + 1) * sizeof(char));
    // then copy. this must be strcpy and no memopy, because strcopy stops at the first \0
    // which is exactly where we need to stop, because dumper could have some data after this \0 that
    // was written before this write.
    strcpy(entry->data, log->dumper);

    // set
    entry->len = data_len;
    entry->next = NULL;

    // update last's next. there exists one because we have a dummy head
    log->last->next = entry;
    // we are the new last
    log->last = entry;

    // including null terminator?
    log->entries_length += data_len;
}


// Returns dummy log content as string (stub)
int get_log(server_log log, char **dst, time_stats *tm_stats) {
    // if you come before the writer, I will not preempt you. but you need to let the writer know you reading:
    // but to allow multiple readers, we only hold the writer if the room is empty


    // before requesting the shared lock with writers
    tm_stats->log_enter = the_time();


    LOCK(c_mutex);
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
        Pthread_cond_wait(&log->gate_cond, &log->c_mutex);
    }
    // so the story goes:
    // the os gives the reader wc_mutex, the problem with just checking writer count, is that there might be a writer waiting for the lock
    // to register itself, but since the os operates on fairness, it has to wait for all the readers to acquire the lock, so we need
    // an asymmetrical way of giving the writer a bias, so we use an atomic writer_arrived, which is best effort asking "Am I blocking any writer?"
    // if so then willingly go to sleep (cond wait) and clear the contest on the mutex for the writer to take over.
    // and when I wake up i'll have the wc mutex so it's ok.


    // register myself
    log->readers_count++;


    if (log->readers_count == 1) {
        // if im the first reader let me open the door for others
        Sem_wait(&log->log_sem);
    }

    // and give the wc mutex
    UNLOCK(c_mutex);

    // if has debug time, then sleep with the lock and do it after log sem to also lock out writers.
    if (log->debug_sleep_time > 0) {
        FSleep(log->debug_sleep_time);
    }

    // before reading
    tm_stats->log_exit = the_time();

    int len = _protected_read(log, dst);


    LOCK(c_mutex);
    log->readers_count--;

    if (log->readers_count == 0) {
        // if im the last to leave then turn off the lights
        // it can be the im the last one but I was not the first so this is why I need semaphores (ownership)
        Sem_post(&log->log_sem);
    }

    UNLOCK(c_mutex);


    return len;
}


// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, time_stats *tm_stats, threads_stats t_stats) {
    // before acquiring the writers lock
    tm_stats->log_enter = the_time();

    // atomic increase to break the mutex fariness
    atomic_inc(&log->writer_arrived);

    LOCK(c_mutex);
    // i'm waiting...
    log->writers_count++;
    // it's ok new readers will go to sleep anyway
    UNLOCK(c_mutex);


    // now wait for the already there readers to finish and block other writers.
    Sem_wait(&log->log_sem);

    if (log->debug_sleep_time > 0) {
        FSleep(log->debug_sleep_time);
    }

    // we acquired the lock
    tm_stats->log_exit = the_time();

    _protected_write(log, tm_stats, t_stats);

    Sem_post(&log->log_sem);

    // atomic decrease do it before wc_mutex to have writer_arrived <= writers_count after lock
    // this is important because we don't want a "lost wakeup" where a reader wakes up and sees the incorrect writer arrived value
    // which is not protected under the wc mutex, i guess it can be moved into the c_mutex also, but what matters is before the broadcast
    // unless I also protect the readers read with atomic read.
    atomic_dec(&log->writer_arrived);


    LOCK(c_mutex);

    log->writers_count--;
    // if no more writers let's notify the readers
    if (log->writers_count == 0) {
        // open the gate for everybody, let the mutex queue block them
        Pthread_cond_broadcast(&log->gate_cond);
    }

    UNLOCK(c_mutex);
}
