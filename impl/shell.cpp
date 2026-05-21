//
// Created by jamal on 15/05/2026.
//

#include "shell.h"

#include <csignal>
#include <iostream>
#include <unistd.h>
#include <commands/pipe.h>
#include <commands/redirect.h>

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

SmallShell::~SmallShell() {
}


Command *SmallShell::_handleIo(string cmd_s, int ioPos) {
    auto cmds = _split(cmd_s, R"(>)");

    //TODO: handle sizes

    Command *cmd1 = CreateCommand(cmds[0].c_str());

    // primitive but effective. x2
    bool isAppend = cmd_s.find(">>") != string::npos;

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

    //TODO: check if alias would be included here

    // Command* cmd2 = new FileWrapperProxy(fileWCtx);
    // this should be translated into [cmd1] | writer
    // in which writer should listen to stdin and write accordingly.
    // return _createPipe(cmd1, cmd2, vector<string> {  });
}

Command *SmallShell::_handlePipe(std::string cmd_s, int pipePos) {
    vector<string> paramsList{};
    // instead of reinventing the wheel, we'll pas it as params

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
        //TODO: handle correctly
        throw std::logic_error("wrong number of pipes");
    }

    if (cmds[0].size() * cmds[1].size() == 0) {
        throw std::logic_error("no pipes?");
    }

    //TODO: perform some validations

    Command *cmd1 = CreateCommand(cmds[0].c_str());

    //TODO: it seems this is not symmetric (for aliases)
    Command *cmd2 = CreateCommand(cmds[1].c_str());

    return _createPipe(cmd1, cmd2, paramsList);
}


Command *SmallShell::_createPipe(Command *cmd1, Command *cmd2, vector<string> paramsList) const {
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
Command *SmallShell::CreateCommand(const char *cmd_line) {
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

    int pipePos = cmd_s.find_first_of('|');
    if (pipePos != std::string::npos) {
        // if pipe, then recursive create both
        return _handlePipe(cmd_s, pipePos);
    }

    int ioPos = cmd_s.find_first_of('>');
    if (ioPos != std::string::npos) {
        // if io, then recursive create both
        return _handleIo(cmd_s, ioPos);
    }


    string candidate = _command(cmd_s);
    // tokenize
    candidate = services->aliases->tokenize(candidate);
    // // perform replacement (maybe also get the index from _command)
    cmd_s.replace(0, cmd_s.find_first_of(" \n"), candidate);

    // extract command
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
    }

    return cmd;
}

void SmallShell::executeCommand(const char *cmd_line) {
    try {
        // this should cover all 3 possible states for checking the jobs
        //TODO: check if they want explicit 3 checks.
        //TODO: waitpid error get's me in an infinite loop
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

            //TODO: save commands to prevent memory leak (and mange them)

            // if is a bg process, then after exec i will get a proc
            if (ctx.get_flags().isBackground && cmd->get_proc() != nullptr) {
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

    // send SIGINT the proc
    SignalProc(proc->pId, SIGINT);

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

void SmallShell::populateCWD() {
    string currentDir = Shell::Utils::get_current_cwd();

    // set the current directory
    wenv->pushDir(currentDir);
}
