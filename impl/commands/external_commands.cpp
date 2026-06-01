//
// Created by jamal on 15/05/2026.
//
#include <unistd.h>
#include <sys/wait.h>

#include "exceptions.h"
#include "external.h"
#include "regex"

#include "utils.h"

using namespace std;

ExternalCommand::ExternalCommand(CommandCtx &ctx): Command(ctx) {
}

void ExternalCommand::execute() {
    auto p = params();

    // normalized args
    auto concated = p->concat();

    static vector<string> pat = {"*", "?"};

    auto cmdId = ctx.get_cmdId();

    bool isComplex = Shell::Utils::contains_or(p->get_raw(), pat);

    const int pid = services->children->fork();

    // parent, handle states
    if (pid > 0) {
        // always create command proc for running children ( so that signals can be forwarded )
        setProc(pid);

        //  ideally it would be better if we redirect this logic into a concrete fg command, but first, we don't have access to the job from command
        // and second, it's going the extra mile for saving 2 line (if were not for compactness i would've done it)

        if (!ctx.get_flags().isBackground) {
            auto proc_mng = services->proc_management;

            // set the current process, since not bg then wait for it
            proc_mng->setAndWaitFgProc(proc);
        }
    } else {
        if (isComplex) {
            // if complex then we are going to run the same command.
            // just pass everything to bash

            string cmdParams = p->concat();
            string bashCmd = cmdId + " " + cmdParams;


            vector<string> bashArgs{
                "/bin/bash",
                "-c",
                bashCmd
            };

            std::vector<char *> argv;
            // for  nullptr
            argv.reserve(bashArgs.size() + 1);

            for (std::string &s: bashArgs) {
                argv.emplace_back(
                    const_cast<char *>(s.c_str())
                );
            }

            argv.emplace_back(nullptr);

            execvp(argv[0], argv.data());
        } else {
            // run the cmd
            vector<string> args = p->list();

            std::vector<char *> argv;

            // +2 one for the null and one for the cmd
            argv.reserve(args.size() + 2);

            // the requested command
            auto cmdId_c = const_cast<char *>(cmdId.c_str());

            argv.push_back(cmdId_c);

            for (std::string &s: args) {
                argv.push_back(
                    const_cast<char *>(s.c_str())
                );
            }

            // must end in NULL
            argv.push_back(nullptr);

            // run regular,
            execvp(cmdId_c, argv.data());
        }
        // args[0] is the path

        // if failed
        throw SyscallException("execvp");
    }
}

bool ExternalCommand::is_external() {
    return true;
}
