//
// Created by jamal on 15/05/2026.
//

#ifndef SHELL_H
#define SHELL_H

#include "command_factory.h"
#include "job_table.h"
#include "commands/all.h"

class SmallShell
{
private:
    SmallShell();

    const std::string APP_NAME = "smash";

    std::shared_ptr<CommandFactory> cmdFactory;

    // working environment (to be shared across commands)
    std::shared_ptr<WorkingEnvironment> wenv = std::make_shared<WorkingEnvironment>((
        APP_NAME
    ));

    // job table
    std::shared_ptr<JobTable> job_table;

    // services
    std::shared_ptr<Services> services;

public:
    Command* CreateCommand(const char* cmd_line);

    SmallShell(SmallShell const&) = delete; // disable copy ctor
    void operator=(SmallShell const&) = delete; // disable = operator
    static SmallShell& getInstance() // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    ~SmallShell();

    void executeCommand(const char* cmd_line);

    std::shared_ptr<WorkingEnvironment> get_wenv() { return wenv; }

    // TODO: add extra methods as needed
};

#endif //SHELL_H
