//
// Created by jamal on 30/06/2026.
//

#ifndef STATS_H
#define STATS_H

#include "segel.h"

typedef struct Threads_stats {
    int id;           // Thread ID
    int stat_req;     // Number of static requests handled
    int dynm_req;     // Number of dynamic requests handled
    int post_req;     // Number of POST requests handled
    int total_req;    // Total number of requests handled
} * threads_stats;

typedef struct Time_stats {
    struct timeval task_arrival;
    struct timeval task_dispatch;
    struct timeval log_enter;
    struct timeval log_exit;
} time_stats;

#endif //STATS_H
