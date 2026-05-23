//
// Created by jamal on 15/05/2026.
//

#include "shell.h"

#include <csignal>
#include <iostream>
#include <unistd.h>
#include <pipe.h>
#include <redirect.h>

#include "exceptions.h"
#include "utils.h"

using namespace std;
using namespace Shell::Utils;

SmallShell::SmallShell() {
    cmdFactory = CommandFactory::getInstance();

    job_table = make_shared<JobTable>();
    auto proc_mng = make_shared<CommandProcManagement>();
    auto aliases = make_shared<Aliases>();
    auto children = make_shared<ChildTracker>();

    services = make_shared<Services>(
        job_table,
        proc_mng,
        aliases,
        children
    );
}

// MUST BE HERE TO AVOID LIB/EXEC NOT SHARING THE STATIC
SmallShell &SmallShell::getInstance() // make SmallShell singleton
{
    static SmallShell instance; // Guaranteed to be destroyed.
    // Instantiated on first use.
    return instance;
}

SmallShell::~SmallShell() {
}

// per assumptions: • I/O redirection and Pipe commands are correctly formatted.
Command *SmallShell::_handleIo(string cmd_s, size_t ioPos) {
    auto cmds = _split(cmd_s, ">");
    if (cmds.size() != 2) {
        throw std::logic_error("wrong number for i/o, although per assumptions, io/pipe are correctly formatted");
    }

    Command *cmd1 = CreateCommand(cmds[0].c_str());

    // primitive but effective. x2
    const bool isAppend = cmd_s.find(">>") != string::npos;

    vector<string> fileWParams{
        cmds[1]
    };

    fileWParams.emplace_back(isAppend ? "append" : "write");

    // the reason to use a custom command is we don't want to register it through the factory and hence leak it.
    CommandCtx fileWCtx(
        "_writer",
        wenv,
        Parameters(fileWParams),
        CommandCtx::CommandFlags{},
        services
    );

    return new FileWrapperProxy(fileWCtx, cmd1);
}

// per assumptions: • I/O redirection and Pipe commands are correctly formatted.
Command *SmallShell::_handlePipe(std::string cmd_s, size_t pipePos) {
    vector<string> paramsList{};
    // instead of reinventing the wheel, we'll pass it as params

    // pipepos+1 is assumed to exist since pipes are correctly formatted.

    bool hasEFlag = cmd_s[pipePos + 1] == '&';
    if (hasEFlag) {
        // that's it we can remove it
        cmd_s.erase(cmd_s.begin() + pipePos + 1);

        // and add it to params
        paramsList.emplace_back("&");
    }

    // we'll ignore isbg at this point
    auto cmds = _split(cmd_s, R"(\|)");

    if (cmds.empty()) {
        throw std::logic_error("no pipes?");
    }
    if (cmds.size() != 2) {
        throw std::logic_error("wrong number of pipes, although per assumptions, io/pipe are correctly formatted");
    }

    // this should also break the recursion cycle. since cmd sizes are strict monotonic dec and hence must hit 0.
    if (cmds[0].size() * cmds[1].size() == 0) {
        throw std::logic_error("no pipes?");
    }

    Command *cmd1 = CreateCommand(cmds[0].c_str());

    // it seems this is not symmetric (for aliases)
    Command *cmd2 = CreateCommand(cmds[1].c_str(), false);

    return _createPipe(cmd1, cmd2, paramsList);
}


Command *SmallShell::_createPipe(Command *cmd1, Command *cmd2, const vector<string> &paramsList) const {
    CommandCtx pipeCtx(
        "pipe",
        wenv,
        Parameters(paramsList),
        CommandCtx::CommandFlags{}, // no flags at this point
        services
    );

    return new PipeCommand(pipeCtx, cmd1, cmd2);
}


/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line, bool tokenizeAlias) {
    // so that we don't modify the original
    const auto raw_cmd_line = const_cast<char *>(cmd_line);

    // normalize
    string cmd_s = _trim(
        string(
            raw_cmd_line
        )
    );


    const bool isBackground = _checkAndRemoveBgCommand(cmd_s);

    //command creation rejected
    if (cmd_s.empty()) return nullptr;

    size_t pipePos = cmd_s.find_first_of('|');
    if (pipePos != std::string::npos) {
        // if pipe, then recursive create both
        return _handlePipe(cmd_s, pipePos);
    }

    size_t ioPos = cmd_s.find_first_of('>');
    if (ioPos != std::string::npos) {
        // if io, then recursive create both
        return _handleIo(cmd_s, ioPos);
    }


    string candidate = _command(cmd_s);

    // since pipes are not symmetrical
    if (tokenizeAlias) {
        // tokenize
        candidate = services->aliases->tokenize(candidate);
    }

    // perform replacement (maybe also get the index from _command)
    cmd_s.replace(0, cmd_s.find_first_of(WHITESPACE), candidate);

    // extract command again, since alias may contain whitespace
    string command = _command(cmd_s);

    // get params
    const vector<string> paramsList = _params(cmd_s);

    const auto params = Parameters(
        paramsList
    );

    CommandCtx ctx(
        command,
        wenv,
        params,
        CommandCtx::CommandFlags{
            // this gets flagged bg even for non external commands, BE CAREFUL!!
            .isBackground = isBackground
        },
        services
    );

    // the problem is this move
    auto cmd = cmdFactory->create(command, ctx);

    if (cmd == nullptr) {
        //then this is an external command.
        cmd = new ExternalCommand(ctx);
    } else {
        // per requirement that only external can run in background. further disabling may occur in i/o or pipe
        cmd->disableBg();
    }

    return cmd;
}

void SmallShell::executeCommand(const char *cmd_line) {
    try {
        // this should cover all 3 possible states for checking the jobs
        // TODO: check if they want explicit 3 checks.
        // TODO: waitpid error gets me in an infinite loop
        job_table->removeFinishedJobs();

        auto cmd = CreateCommand(cmd_line);

        if (cmd == nullptr) {
            //creation rejected, handle.
            return;
        }

        const CommandCtx &ctx = cmd->get_ctx();

        try {
            // exec + prepare are unifed under run
            cmd->run();

            // if is a bg process, then after exec i will get a proc
            if (ctx.get_flags().isBackground && cmd->get_proc() != nullptr) {
                // as per requirements interpretation i'll do another check
                job_table->removeFinishedJobs();

                //then add it to the job entry
                job_table->addJob(
                    cmd_line,
                    cmd->get_proc()
                );
            }
        } catch (const CommandDomainError &e) {
            std::cerr << APP_NAME << " error: " << ctx.get_cmdId() << ": " << e.what() << std::endl;

            // report the error and quit
            if (services->children->isChild) {
                exit(0);
            }
        }
        // we are finished with this cmd
        delete cmd;
    } catch (const SyscallException &e) {
        string errorMsg = APP_NAME + " error: " + e.what() + " failed";

        perror(errorMsg.c_str());
        // I need a global child error handling here.
        if (services->children->isChild) {
            exit(0);
        }
    }
    catch (const std::exception &e) {
        // TODO: what do general exception means? (they didnt originate from the app)
        std::cerr << "general error: " << e.what() << std::endl;

        if (services->children->isChild) {
            exit(0);
        }

        throw e;
        // std::cerr << APP_NAME << " error: " << ctx.get_cmdId() << ":" << e.what() << std::endl;
    }
}

void SmallShell::kill_fg_proc() {
    // get the proc
    auto proc = get_fg_proc();

    // send SIGKILL the proc
    SignalProc(proc->pId, SIGKILL);

    // proc was killed
    // note; this should not matter, since now we are removing it from job entry and it should be freed, but keeping it for assertion purposes
    proc->wasKilled = true;

    // clear the fg
    services->proc_management->clearFgProc();
}

void SmallShell::forward_signal(int sigNum) {
    const std::string termName = get_terminalName();

    std::cout << termName << ": " << "got ctrl-C" << std::endl;

    auto proc = get_fg_proc();

    // if there is any currently running fg proc
    if (proc != nullptr) {
        kill_fg_proc();

        std::cout << termName << ":" << " process " << proc->pId << " was killed" << std::endl;
    }
}
