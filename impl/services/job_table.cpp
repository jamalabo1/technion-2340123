//
// Created by jamal on 15/05/2026.
//

#include "../../include/services/job_table.h"
#include "utils.h"


using namespace std;

JobTable::JobTable() : jobs(), jobsCount(0)
{
}


void JobTable::addJob(
    const std::string& cmd_line,
    CommandProc* proc
)
{
    // first if we reached this point, this command must have a process
    if (proc == nullptr)
    {
        throw std::logic_error("proc is null");
    }

    int jobId = jobsCount++;

    proc->assignedJobId = jobId;

    jobs.emplace_back(
        jobId,
        proc,
        cmd_line
    );
}

void JobTable::printJobsList()
{
    for (auto& job : jobs)
    {
        std::cout << "[" << job.get_id() << "]" << " ";

        job.printCmdLine();

        std::cout << std::endl;
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
}

void JobTable::removeJobById(int jobId)
{
    // TODO: use a smarted data structure
    jobs.erase(
        std::remove_if(
            jobs.begin(), jobs.end(), [jobId](JobEntry& entry)
            {
                return entry.get_id() == jobId;
            }),
        jobs.end());
}

JobEntry* JobTable::get_maxIdJob()
{
    if (jobs.empty()) return nullptr;
    return &jobs[jobs.size() - 1];
}

JobEntry* JobTable::get_jobById(int jobId)
{
    //TODO: smarted data structure
    for (auto& entry : jobs)
    {
        if (entry.get_id() == jobId) return &entry;
    }
    return nullptr;
}

void JobTable::killAndRemoveAllJobs()
{
    std::cout << "sending SIGKILL signal to " << jobs.size() << " jobs:" << std::endl;

    for (auto& job : jobs)
    {
        int jobPid = job.get_pId();

        std::cout << jobPid << ": ";

        job.printCmdLine();

        std::cout << std::endl;

        Shell::Utils::KillProc(jobPid);
    }
    // this should free the objs
    jobs = vector<JobEntry>();
}
