//
// Created by jamal on 15/05/2026.
//

#include "shell.h"

#include <iostream>

#include "exceptions.h"
#include "utils.h"
#include "commands/external.h"

using namespace std;
using namespace Shell::Utils;

SmallShell::SmallShell()
{
    // TODO: add your implementation
    cmdFactory = CommandFactory::getInstance();

    job_table = make_shared<JobTable>();

    services = make_shared<Services>(
        job_table
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
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    size_t firstSpace = cmd_s.find_first_of(" \t");
    string paramsRaw;
    if (firstSpace != string::npos)
    {
        // Substr from char after space to end
        paramsRaw = cmd_s.substr(firstSpace + 1);
    }
    else
    {
        // No arguments, only command
        paramsRaw = "";
    }

    //TODO: use "tokenizer" to change aliases here.

    std::vector<std::string> paramsList = _split(paramsRaw);

    bool isBg = false;
    //TODO: had it do it, but change this bullshit asap
    if (paramsList.size() > 0)
    {
        isBg = paramsList[paramsList.size() - 1] == "&";

        // and if i know it's bg them remove it our right from the list don't propagate
        if (isBg)
        {
            paramsList.erase(paramsList.end() - 1);
        }
    }

    auto* params = new Parameters(
        paramsRaw,
        paramsList
    );


    CommandCtx ctx(
        cmd_s,
        firstWord,
        wenv,
        params,
        CommandCtx::CommandFlags{
            .isBackground = isBg
        },
        services
    );

    auto cmd = cmdFactory->create(firstWord, ctx);
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

    try
    {
        cmd->execute();

        // if is a bg process, then after exec i will get a proc
        if (cmd->get_ctx().get_flags().isBackground)
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
    catch (const std::exception& e)
    {
        const CommandCtx& ctx = cmd->get_ctx();

        // const string terminalName = ctx.get_wenv()->getAppName();

        std::cerr << APP_NAME << " error: " << ctx.get_cmdId() << ":" << e.what() << std::endl;
    }
    // Please note that you must fork smash process for some commands (e.g., external commands....)
}
