#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "shell.h"

#include "signals.h"

int main(int argc, char *argv[]) {
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-C handler");
    }


    SmallShell &smash = SmallShell::getInstance();

    std::shared_ptr<WorkingEnvironment> env = smash.get_wenv();
    while (true) {
        std::cout << env->getTerminalName() << "> ";
        std::string cmd_line;
        std::getline(std::cin, cmd_line);
        smash.executeCommand(cmd_line.c_str());
    }

    return 0;
}