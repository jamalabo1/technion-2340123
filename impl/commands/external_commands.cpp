//
// Created by jamal on 15/05/2026.
//
#include <unistd.h>
#include <sys/wait.h>

#include "exceptions.h"
#include "commands/external.h"
#include "regex"

#include "utils.h"

using namespace std;

ExternalCommand::ExternalCommand(CommandCtx& ctx): Command(ctx)
{
}

void ExternalCommand::execute()
{
    auto p = params();

    // normalized args
    auto concated = p->concat();

    std::regex complex_ptrn(R"([?*])");

    //TODO: here's the flag
    bool isComplex = Shell::Utils::regex_contains(concated, complex_ptrn);


    int pid = ctx.get_services()->children->fork();

    // parent, handle states
    if (pid != 0)
    {
        // always create command proc for running children ( so that signals can be forwarded )
        proc = new CommandProc();
        proc->pId = pid;

        // TODO: ideally it would be better if we redirect this logic into a concrete fg command, but first, we don't have access to the job from command
        // and second, it's going the extra mile for saving 2 line (if were not for compactness i would've done it)

        if (!ctx.get_flags().isBackground)
        {
            auto proc_mng = ctx.get_services()->proc_management;

            // set the current process, since not bg then wait for it
            proc_mng->setAndWaitFgProc(proc);
        }
    }
    else
    {
        // child,
        setpgrp();

        // run the cmd
        vector<string> args = p->list();


        std::vector<char*> argv;

        // +2 one for the null and one for the cmd
        argv.reserve(args.size() + 2);

        // the requested command
        auto cmdId = ctx.get_cmdId();
        auto cmdId_c = const_cast<char*>(cmdId.c_str());

        argv.push_back(cmdId_c);

        for (std::string& s : args)
        {
            argv.push_back(
                const_cast<char*>(s.c_str())
            );
        }

        // must end in NULL
        argv.push_back(nullptr);

        //TODO: here modify if using bash (only add two params and change cmdId_c)

        // args[0] is the path
        execvp(cmdId_c, argv.data());

        // if failed
        throw SyscallException("execvp");
    }
}
