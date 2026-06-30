//
// Created by jamal on 30/06/2026.
//

#ifndef FORMATTER_H
#define FORMATTER_H

#include "stats.h"

int append_job_log(char *buf, time_stats tm_stats);

// Appends the per-thread statistics to buf.
int append_thread_log(char *buf, threads_stats t_stats);

// Appends the full statistics block to an HTTP response header.
int append_stats(char *buf, threads_stats t_stats, time_stats tm_stats);

#endif //FORMATTER_H
