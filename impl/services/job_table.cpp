//
// Created by jamal on 15/05/2026.
//

#include "services/job_table.h"
#include "utils.h"


using namespace std;

JobTable::JobTable() : _jobs()
{
}


void JobTable::addJob(
    const std::string& cmd_line,
    const shared_ptr<CommandProc>& proc
)
{
    // first if we reached this point, this command must have a process
    if (proc == nullptr)
    {
        throw std::logic_error("proc is null");
    }

    int jobId = _jobs.getMaxKey() + 1;

    proc->assignedJobId = jobId;

    const JobEntry entry(
        jobId,
        proc,
        cmd_line
    );

    _jobs.insert(jobId, entry);
}

void JobTable::printJobsList()
{
    //TODO: check interpretation

    // first make sure it's not stale
    removeFinishedJobs();

    _jobs.inorder([](const shared_ptr<JobEntry>& job)
    {
        std::cout << "[" << job->get_id() << "]" << " ";

        job->printCmdLine();

        std::cout << std::endl;
    });
}

void JobTable::removeFinishedJobs()
{
    // don't remove while looping over the vector (sounds bad)
    vector<int> markedForDeletion;

    _jobs.inorder([&markedForDeletion](const shared_ptr<JobEntry>& job)
    {
        // check if child finished
        if (Shell::Utils::check_child(job->get_pId()) == 0)
        {
            markedForDeletion.push_back(job->get_id());
        }
    });

    for (const int& jobId : markedForDeletion)
    {
        removeJobById(jobId);
    }
}

void JobTable::removeJobById(int jobId)
{
    _jobs.remove(jobId);
}

shared_ptr<JobEntry> JobTable::get_maxIdJob() const
{
    if (_jobs.isEmpty()) return nullptr;
    // call find max (tree must be non empty)
    return _jobs.findMax().second;
}

shared_ptr<JobEntry> JobTable::get_jobById(int jobId) const
{
    return _jobs.find(jobId);
}

void JobTable::killAndRemoveAllJobs()
{
    std::cout << "sending SIGKILL signal to " << _jobs.size() << " jobs:" << std::endl;

    //TODO: check interpretation

    // first make sure it's not stale
    removeFinishedJobs();

    _jobs.inorder([](const shared_ptr<JobEntry>& job)
    {
        int jobPid = job->get_pId();

        std::cout << jobPid << ": ";

        job->printCmdLine();

        std::cout << std::endl;

        Shell::Utils::KillProc(jobPid);
    });

    // empty the avl (it will call destructor on maneged pointers)
    _jobs = Avl<int, JobEntry>();
}
