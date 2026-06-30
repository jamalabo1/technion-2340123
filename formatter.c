#include <stdio.h>

#include "stats.h"
//
// Created by jamal on 30/06/2026.
//
// Appends the per-job (timing) statistics to buf.

#include "formatter.h"
#include "segel.h"

int append_job_log(char *buf, time_stats tm_stats) {
    int offset = strlen(buf);

    offset += sprintf(buf + offset, "Stat-Req-Arrival:: %ld.%06ld\r\n",
                      tm_stats.task_arrival.tv_sec, tm_stats.task_arrival.tv_usec);
    offset += sprintf(buf + offset, "Stat-Req-Dispatch:: %ld.%06ld\r\n",
                      tm_stats.task_dispatch.tv_sec, tm_stats.task_dispatch.tv_usec);
    offset += sprintf(buf + offset, "Stat-Log-Arrival:: %ld.%06ld\r\n",
                      tm_stats.log_enter.tv_sec, tm_stats.log_enter.tv_usec);
    offset += sprintf(buf + offset, "Stat-Log-Dispatch:: %ld.%06ld\r\n",
                      tm_stats.log_exit.tv_sec, tm_stats.log_exit.tv_usec);
    return offset;
}

// Appends the per-thread statistics to buf.
int append_thread_log(char *buf, threads_stats t_stats) {
    int offset = strlen(buf);

    offset += sprintf(buf + offset, "Stat-Thread-Id:: %d\r\n", t_stats->id);
    offset += sprintf(buf + offset, "Stat-Thread-Count:: %d\r\n", t_stats->total_req);
    offset += sprintf(buf + offset, "Stat-Thread-Static:: %d\r\n", t_stats->stat_req);
    offset += sprintf(buf + offset, "Stat-Thread-Dynamic:: %d\r\n", t_stats->dynm_req);
    offset += sprintf(buf + offset, "Stat-Thread-Post:: %d\r\n\r\n", t_stats->post_req);
    return offset;
}

// Appends the full statistics block to an HTTP response header.
int append_stats(char *buf, threads_stats t_stats, time_stats tm_stats) {
    append_job_log(buf, tm_stats);
    return append_thread_log(buf, t_stats);
}
