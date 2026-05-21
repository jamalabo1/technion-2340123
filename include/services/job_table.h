//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_LIST_H
#define JOB_LIST_H

#include <algorithm>
#include <vector>
#include <memory>

#include "../job.h"
#include "ds/avl.h"

class JobTable
{
public:
    // std::vector<JobEntry> jobs;

    Avl<int, JobEntry> _jobs;

    // TODO: Add your data members
public:
    JobTable();

    // ~JobTable()
    // {
    // };

    void addJob(
        const std::string& cmd_line,
        const std::shared_ptr<CommandProc>& cmd
    );

    void printJobsList();

    // void killAllJobs()
    // {
    // };

    void removeFinishedJobs();

    // JobEntry* getJobById(int jobId)
    // {
    // };

    void removeJobById(int jobId);


    std::shared_ptr<JobEntry> get_maxIdJob() const;
    std::shared_ptr<JobEntry> get_jobById(int jobId) const;


    void killAndRemoveAllJobs();

    //
    // JobEntry* getLastJob(int* lastJobId);
    //
    // std::shared_ptr<JobEntry> getLastStoppedJob(int* jobId);
    //
    // TODO: Add extra methods or modify exisitng ones as needed
};


#endif //JOB_LIST_H
