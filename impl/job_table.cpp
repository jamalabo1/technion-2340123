//
// Created by jamal on 15/05/2026.
//

#include "job_table.h"

#include <iostream>

#include "utils.h"


using namespace std;

JobTable::JobTable() : jobs()
{
}

void JobTable::addJob(
    std::string cmd_line,
    CommandProc* proc
)
{
    // first we if we reached this point, this command must have a process
    if (proc == nullptr)
    {
        throw std::logic_error("proc is null");
    }

    jobs.push_back(
        JobEntry(
            jobsCount++,
            proc,
            cmd_line
        )
    );
}

void JobTable::printJobsList()
{
    for (auto& job : jobs)
    {
        auto rawCmd = job.get_cmdLine();

        std::cout << "[" << job.get_id() << "]" << " " << rawCmd << std::endl;
    }
}

void JobTable::removeFinishedJobs()
{
    // don't remove while looping over the vector (sounds bad)
    vector<int> markedForDeletion;

    // poll the kernel for child status
    for (auto& job : jobs)
    {
        // check if child finished
        if (Shell::Utils::check_child(job.get_pId()) == 0)
        {
            markedForDeletion.push_back(job.get_id());
        }
    }
    for (const int& jobId : markedForDeletion)
    {
        removeJobById(jobId);
    }

    // remove while itter
    // jobs.erase(
    //     std::remove_if(
    //         jobs.begin(), jobs.end(),
    //         [](const JobEntry& entry)
    //         {
    //             return Shell::Utils::check_child(entry.get_pId()) >= 0;
    //         }),
    //     jobs.end());
}
