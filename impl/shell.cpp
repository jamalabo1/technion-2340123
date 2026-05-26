//
// Created by jamal on 15/05/2026.
//

#include "shell.h"

#include <csignal>
#include <iostream>
#include <unistd.h>
#include "pipe.h"
#include "redirect.h"

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


// per assumptions: • I/O redirection and Pipe commands are correctly formatted.
unique_ptr<Command> SmallShell::_handleIo(string cmd_s, size_t ioPos) {
    auto cmds = _split(cmd_s, ">");
    if (cmds.size() != 2) {
        throw std::logic_error("wrong number for i/o, although per assumptions, io/pipe are correctly formatted");
    }

    unique_ptr<Command> cmd1 = CreateCommand(cmds[0]);

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

    return unique_ptr<Command>(new FileWrapperProxy(fileWCtx, std::move(cmd1)));
}

// per assumptions: • I/O redirection and Pipe commands are correctly formatted.
unique_ptr<Command> SmallShell::_handlePipe(std::string cmd_s, size_t pipePos) {
    vector<string> paramsList{};
    // instead of reinventing the wheel, we'll pass it as params

    // pipe pos+1 is assumed to exist since pipes are correctly formatted.

    bool hasEFlag = cmd_s[pipePos + 1] == '&';
    if (hasEFlag) {
        // that's it we can remove it
        cmd_s.erase(cmd_s.begin() + pipePos + 1);

        // and add it to params
        paramsList.emplace_back("&");
    }

    // NOTE: must use _lsplit to handle nested pipes, like a | b | c
    // this way a | b is treated as cmd2 and is recursively called.
    // but make sure to insure alias is disabled all the way.

    // we'll ignore isbg at this point
    auto cmds = _lpart(cmd_s, pipePos);

    if (cmds.empty() || cmds.size() != 2) {
        throw std::logic_error("wrong number of pipes, although per assumptions, io/pipe are correctly formatted");
    }

    // this should also break the recursion cycle. since cmd sizes are strict monotonic dec and hence must hit 0.
    if (cmds[0].size() * cmds[1].size() == 0) {
        throw std::logic_error("no pipes?");
    }

    // create the commands
    unique_ptr<Command> cmd1 = CreateCommand(cmds[0]);
    unique_ptr<Command> cmd2 = CreateCommand(cmds[1]);

    CommandCtx pipeCtx(
        "_pipe",
        wenv,
        Parameters(paramsList),
        CommandCtx::CommandFlags{}, // no flags at this point
        services
    );

    return unique_ptr<PipeCommand>(new PipeCommand(pipeCtx, std::move(cmd1), std::move(cmd2)));
}

std::string SmallShell::_tokenizeCmd(const char *cmd_line) {
    /**
     * so it goes like this. [a]& is stripped of & and matched.
     * [a]|, [a]< are matched up until the first char
     * so in priority order, having | or < we match using the second mode, so
     * s&|cat is not matched against alias s because it has &, and it's not recursive, so we treat s& as alias
     *
     */

    // so that we don't modify the original
    const auto raw_cmd_line = const_cast<char *>(cmd_line);

    // normalize
    string cmd_s = _trim(
        string(
            raw_cmd_line
        )
    );

    // const bool isBackground = _checkAndRemoveBgCommand(cmd_s);

    // so save myself all the headache, i'll instead match char char, until I hit | > or &
    // then i'll stop, return the pos and what was matched.
    // then tokenize using alias
    // then add a whitespace to save my self the trouble afterward. since [w]| or [w]& wouldn't matter


    pair<string, size_t> cord = _commandWithPos(cmd_s);

    string candidate = cord.first;

    candidate = services->aliases->tokenize(candidate);

    // replace with matched alias with a whitespace.
    cmd_s.replace(0, cord.second, candidate + " ");

    return cmd_s;
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
unique_ptr<Command> SmallShell::CreateCommand(std::string cmd_s) {
    // normalize
    cmd_s = _trim(
        cmd_s
    );

    // so that we don't modify the original
    const bool isBackground = _checkAndRemoveBgCommand(cmd_s);

    //command creation rejected
    if (cmd_s.empty()) return nullptr;

    // extract command
    string command = _command(cmd_s);

    // ONLY if the command is alias ... then we disable |> parsing since it can appear in the alias.
    if (command != "alias") {
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
    }


    CommandCtx ctx(
        command,
        wenv,
        // create params
        Parameters(
            _params(cmd_s)
        ),
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

    return unique_ptr<Command>(cmd);
}

void SmallShell::executeCommand(const char *cmd_line) {
    try {
        // as per before any command
        job_table->removeFinishedJobs();

        auto tokenized_cmd = _tokenizeCmd(cmd_line);

        auto cmd = CreateCommand(tokenized_cmd);

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
