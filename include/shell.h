//
// Created by jamal on 15/05/2026.
//

#ifndef SHELL_H
#define SHELL_H

#include "command_factory.h"
#include "job_table.h"
#include "all.h"

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

private:
    std::unique_ptr<Command> _handleIo(std::string cmd_s, size_t ioPos);

    std::unique_ptr<Command> _handlePipe(std::string cmd_s, size_t pipePos);

    std::string _tokenizeCmd(const char* cmd_line);

public:
    std::unique_ptr<Command> CreateCommand( std::string cmd_line);

    SmallShell(SmallShell const&) = delete; // disable copy ctor
    void operator=(SmallShell const&) = delete; // disable = operator

    static SmallShell& getInstance();

    void executeCommand(const char* cmd_line);

    std::string get_terminalName() const { return wenv->getTerminalName(); }

    std::shared_ptr<CommandProc> get_fg_proc() const { return services->proc_management->getFgProc(); }

    void kill_fg_proc();

    void forward_signal(int sigNum);
};

#endif //SHELL_H
