//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#include <unistd.h>

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"

/*
 * ChangeTerminalNameCommand
 */
REGISTER_COMMAND(ChangeTerminalNameCommand);

void ChangeTerminalNameCommand::execute() {
    // get the current working env
    auto wenv = ctx.get_wenv();

    auto termName = params()
            ->firstOrDefault();

    // if the params is empty
    if (termName.empty()) {
        // reset to default if no arguments provided
        wenv->resetTerminalName();
    } else {
        // otherwise set to first param
        wenv->setTerminalName(termName);
    }
}

/*
 * JobsCommand
 */
REGISTER_COMMAND(JobsCommand);

void JobsCommand::execute() {
    auto services = ctx.get_services();

    services->table->printJobsList();
}

/*
 * QuitCommand
 */
REGISTER_COMMAND(QuitCommand)

void QuitCommand::execute() {
    bool hasKill = params()->firstOrDefault() == "kill";

    if (hasKill) {
        auto termName = ctx.get_wenv()->getTerminalName();

        // print current terminal name
        std::cout << termName << ": ";

        // hand it to the table for further printing and handling (although debatable if the table should be the one to do this)
        ctx.get_services()->table->killAndRemoveAllJobs();
    }

    std::exit(0);
}

/*
 * ShowPidCommand
 */
REGISTER_COMMAND(ShowPidCommand);

void ShowPidCommand::execute() {
    std::cout << "smash pid is" << getpid() << std::endl;
}

/*
 * GetCurrDirCommand
 */
REGISTER_COMMAND(GetCurrDirCommand);

void GetCurrDirCommand::execute() {
    // according to man7, nullptr, 0 in posix glibc ext, dynamically allocates the buffer's size.
    // but it's our responsibility to free it
    char *buf = getcwd(nullptr, 0);

    std::cout << buf << std::endl;

    free(buf);
}

/*
 * ChangeDirCommand
 */
REGISTER_COMMAND(ChangeDirCommand);

void ChangeDirCommand::execute() {
    auto path = params()->singleOrDefault<CommandTooManyArguments>();
    if (path.empty()) return;

    auto wenv = ctx.get_wenv();

    if (path == "-") {
        try {
            path = wenv->getLastWorkingDir();
        } catch (const std::exception&) {
            throw CommandErrorMessage("OLDPWD not set");
        }
    }

    // current state is [X]->[Y]

    if (chdir(path.c_str()) == -1) {
        throw SyscallException("chdir");
    }

    // we want the new one to be [Y]->[X]
    // so we need to push [Y]
    wenv->pushDir(path);
}
