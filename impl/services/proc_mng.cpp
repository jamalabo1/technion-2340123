//
// Created by jamal on 16/05/2026.
//
#include "services/proc_mng.h"

#include <cstddef>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <sys/wait.h>

#include "exceptions.h"

CommandProcManagement::CommandProcManagement()
{
    current_fg_proc = nullptr;
}

void CommandProcManagement::setFgProc(CommandProc* proc)
{
    current_fg_proc = proc;
}

CommandProc* CommandProcManagement::getFgProc() const
{
    return current_fg_proc;
}

void CommandProcManagement::clearFgProc()
{
    setFgProc(nullptr);
}

void CommandProcManagement::waitFgProc()
{
    // get the proc
    auto proc = getFgProc();

    if (proc == nullptr)
    {
        throw std::logic_error("no proc to wait on");
    }
    if (proc->wasKilled)
    {
        throw std::logic_error("proc was already killed");
    }

    // NOTE: YOU MUST MAKE SURE IT'S REMOVED FROM THE JOB TABLE TO PREVENT DOUBLE WAITPID

    // else wait for the child to finish like regular
    if (waitpid(proc->pId, nullptr, 0) == -1) {
        throw SyscallException("waitpid");
    };

    // clear after wait
    clearFgProc();
}

void CommandProcManagement::setAndWaitFgProc(CommandProc* proc)
{
    setFgProc(proc);

    waitFgProc();
}
