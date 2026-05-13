//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_LIST_H
#define JOB_LIST_H

#include<memory>
#include <vector>
#include "job.h"

class JobTable {
public:


    // TODO: Add your data members
public:
    JobTable();

    ~JobTable();

    void addJob(Command *cmd, bool isStopped = false);

    void printJobsList();

    void killAllJobs();

    void removeFinishedJobs();

    JobEntry *getJobById(int jobId);

    void removeJobById(int jobId);

    JobEntry *getLastJob(int *lastJobId);

    std::shared_ptr<JobEntry> getLastStoppedJob(int *jobId);
    // TODO: Add extra methods or modify exisitng ones as needed
};

#endif //JOB_LIST_H
