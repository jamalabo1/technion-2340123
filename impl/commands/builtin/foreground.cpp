//
// Created by jamal on 16/05/2026.
//

#include "commands/built_in.h"
#include "exceptions.h"


REGISTER_COMMAND(ForegroundCommand);

void ForegroundCommand::prepare()
{
    // single or default -1
    auto jobId_s = params()
        ->singleOrDefault<CommandInvalidArguments>();

    try
    {
        if (jobId_s.empty())
        {
            //0 as special value
            jobId = 0;
        }
        else
        {
            jobId = Shell::Utils::parseInt(jobId_s);
            // the only valid range is [1, inf]
            if (jobId <= 0) throw CommandInvalidArguments();
        }
    }
    catch (std::exception&)
    {
        //NOTE: I think it's of this reason it's better if I keep it split here instead of merging
        throw CommandInvalidArguments();
    }
}

void ForegroundCommand::execute()
{
    auto job =
        jobId == 0
            ? services->table->get_maxIdJob()
            : services->table->get_jobById(jobId);

    if (job == nullptr)
    {
        if (jobId > 0)
        {
            throw CommandJobNotFoundException(jobId);
        }
        throw CommandErrorMessage("jobs list is empty");
    }

    auto proc = job->get_proc();

    if (proc == nullptr)
    {
        throw std::logic_error("how is a job without a proc?");
    }
    // print the current cwd
    job->printCmdWithPid();

    // set the currently running fg process then wait for it
    services->proc_management->setAndWaitFgProc(proc);

    // remove from jobs after since waitpid could fail
    services->table->removeJobById(proc->assignedJobId);
}
