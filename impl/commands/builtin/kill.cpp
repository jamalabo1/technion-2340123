//
// Created by jamal on 16/05/2026.
//

#include <commands/built_in.h>

#include "exceptions.h"
#include "utils.h"

REGISTER_COMMAND(KillCommand);

using Shell::Utils::parseInt;
using Shell::Utils::SignalProc;

void KillCommand::prepare()
{
    auto pList = params()->list();
    if (pList.size() != 2) throw CommandInvalidArguments();

    // strict matching, although ideally it would be regex matched, but I want the out-of-bound to be handled by linux syscall fail.
    auto sigExpr = pList[0];
    if (sigExpr.empty() || sigExpr[0] != '-')
    {
        throw CommandInvalidArguments();
    }

    try
    {
        sigNum = parseInt(sigExpr.substr(1));
        jobId = parseInt(pList[1]);
    }
    catch (std::exception&)
    {
        throw CommandInvalidArguments();
    }

    // the only valid range is [1, inf]
    if (jobId < 1) throw CommandInvalidArguments();
}


void KillCommand::execute()
{
    auto job = services->table->get_jobById(jobId);
    if (job == nullptr) throw CommandJobNotFoundException(jobId);

    const int pid = job->get_pId();

    // no need to remove from job entry, no waitpid was done.
    SignalProc(pid, sigNum);

    // if successes we are here, otherwise, threw an exception
    std::cout << "signal number " << sigNum << " was sent to pid " << pid << std::endl;
}
