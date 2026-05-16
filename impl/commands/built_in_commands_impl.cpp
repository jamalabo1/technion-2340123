//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"


REGISTER_COMMAND(ChangeTerminalNameCommand);

void ChangeTerminalNameCommand::execute()
{
    // get the current working env
    auto wenv = ctx.get_wenv();

    auto termName = params()
        ->firstOrDefault();

    // if the params is empty
    if (termName.empty())
    {
        // reset to default if no arguments provided
        wenv->resetTerminalName();
    }
    else
    {
        // otherwise set to first param
        wenv->setTerminalName(termName);
    }
}


REGISTER_COMMAND(JobsCommand);

void JobsCommand::execute()
{
    auto services = ctx.get_services();

    services->table->printJobsList();
}

REGISTER_COMMAND(QuitCommand)

void QuitCommand::execute()
{
    bool hasKill = params()->firstOrDefault() == "kill";

    if (hasKill)
    {
        auto termName = ctx.get_wenv()->getTerminalName();

        // print current terminal name
        std::cout << termName << ": ";

        // hand it to the table for further printing and handling (although debatable if the table should be the one to do this)
        ctx.get_services()->table->killAndRemoveAllJobs();
    }

    std::exit(0);
}

