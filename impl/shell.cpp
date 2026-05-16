//
// Created by jamal on 15/05/2026.
//

#include "shell.h"

#include <csignal>
#include <iostream>

#include "exceptions.h"
#include "utils.h"

using namespace std;
using namespace Shell::Utils;

SmallShell::SmallShell()
{
    cmdFactory = CommandFactory::getInstance();

    job_table = make_shared<JobTable>();
    auto proc_mng = make_shared<CommandProcManagement>();
    auto aliases = make_shared<Aliases>();

    services = make_shared<Services>(
        job_table,
        proc_mng,
        aliases
    );
}

SmallShell::~SmallShell()
{
    // TODO: add your implementation
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command* SmallShell::CreateCommand(const char* cmd_line)
{
    const auto raw_cmd_line = const_cast<char*>(cmd_line);

    // check before removing
    const bool isBackground = _isBackgroundComamnd(cmd_line);

    // remove the bg sign
    _removeBackgroundSign(
        raw_cmd_line
    );

    // normalize
    string cmd_s = _trim(
        string(
            raw_cmd_line
        )
    );

    string candidate = _command(cmd_s);
    candidate = services->aliases->tokenize(candidate);
    // // perform replacement (maybe also get the index from _command)
    cmd_s.replace(0, cmd_s.find_first_of(" \n"), candidate);

    // TODO: tokenize here using aliases.

    // extract command
    string command = _command(cmd_s);

    // get params
    const vector<string> paramsList = _params(cmd_s);

    auto* params = new Parameters(
        paramsList
    );


    CommandCtx ctx(
        command,
        wenv,
        params,
        CommandCtx::CommandFlags{
            .isBackground = isBackground
        },
        services
    );

    auto cmd = cmdFactory->create(command, ctx);

    if (cmd == nullptr)
    {
        //then this is an external command.
        cmd = new ExternalCommand(ctx);
    }

    return cmd;
}

void SmallShell::executeCommand(const char* cmd_line)
{
    // this should cover all 3 possible states for checking the jobs
    //TODO: check if they want explicit 3 checks.
    job_table->removeFinishedJobs();

    Command* cmd = CreateCommand(cmd_line);
    const CommandCtx& ctx = cmd->get_ctx();

    try
    {
        cmd->prepare();

        cmd->execute();

        // if is a bg process, then after exec i will get a proc
        if (ctx.get_flags().isBackground)
        {
            //then add it to the job entry
            job_table->addJob(
                cmd_line,
                cmd->get_proc()
            );
        }
    }
    catch (const SyscallException& e)
    {
        string errorMsg = APP_NAME + " error: " + e.what() + " failed";

        perror(errorMsg.c_str());
    }
    catch (const CommandDomainError& e)
    {
        std::cerr << APP_NAME << " error: " << ctx.get_cmdId() << ":" << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        // TODO: what do general exception means? (they didnt originate from the app)
        throw e;
        // std::cerr << APP_NAME << " error: " << ctx.get_cmdId() << ":" << e.what() << std::endl;
    }
}

void SmallShell::kill_fg_proc()
{
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

void SmallShell::forward_signal(int sigNum)
{
    const std::string termName = get_terminalName();

    std::cout << termName << ": " << "got ctrl-C" << std::endl;

    auto proc = get_fg_proc();

    // if there is any currently running fg proc
    if (proc != nullptr)
    {
        std::cout << "forwarding" << std::endl;

        kill_fg_proc();

        std::cout << termName << ":" << "process " << proc->pId << " was killed" << std::endl;
    }
}
