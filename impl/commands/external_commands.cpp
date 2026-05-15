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


    int pid = fork();

    // parent, handle states
    if (pid != 0)
    {
        if (pid == -1)
        {
            throw SyscallException("fork");
        }

        // if background then don't wait
        if (ctx.get_flags().isBackground)
        {
            //TODO: temp for testing
            proc = new CommandProc();
            proc->pId = pid;

            return;
        }

        // else wait for the child to finish like regular
        waitpid(pid, NULL, 0);
    }
    else
    {
        // child,
        setpgrp();

        // run the cmd
        vector<string> args = p->list();


        std::vector<char*> argv;
        argv.reserve(args.size() + 2);

        // the requested command
        auto cmdId = const_cast<char*>(ctx.get_cmdId().c_str());

        argv.push_back(cmdId);

        for (std::string& s : args)
        {
            argv.push_back(
                const_cast<char*>(s.c_str())
            );
        }

        // must end in NULL
        argv.push_back(nullptr);

        // args[0] is the path
        execvp(cmdId, argv.data());

        // if failed
        throw SyscallException("execvp");
    }


    // if (isComplex)
    // {
    //     // run on bash
    // }
    // else
    // {
    //     // run on exec directly
    // }
}
