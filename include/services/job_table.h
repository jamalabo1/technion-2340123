//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_LIST_H
#define JOB_LIST_H

#include <algorithm>
#include <vector>
#include <memory>

#include "job.h"
#include "avl.h"

class JobTable
{
public:
    Avl<int, JobEntry> _jobs;
public:
    JobTable();

    void addJob(
        const std::string& cmd_line,
        const std::shared_ptr<CommandProc>& cmd
    );

    void printJobsList();


    void removeFinishedJobs();


    void removeJobById(int jobId);


    std::shared_ptr<JobEntry> get_maxIdJob() const;
    std::shared_ptr<JobEntry> get_jobById(int jobId) const;


    void killAndRemoveAllJobs();
};


#endif //JOB_LIST_H
