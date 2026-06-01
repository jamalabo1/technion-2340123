#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "shell.h"

#include "signals.h"


int main(int argc, char *argv[]) {
    // if (signal(SIGINT, ctrlCHandler) == SIG_ERR)
    // {
    //     perror("smash error: failed to set ctrl-C handler");
    // }
    // instead of the regular signal, i'll use sigaction to make sure SA_RESTART is set
    struct sigaction sa{};
    sa.sa_handler = ctrlCHandler; // the handler
    sa.sa_flags = SA_RESTART; // explicitly request system call restart to avoid pre-call handling of waitpid -1.

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        perror("smash error: failed to set ctrl-C handler");
    }

    SmallShell &smash = SmallShell::getInstance();


    while (true) {
        std::string termName = smash.get_terminalName();
        std::cout << termName << "> ";

        std::string cmd_line;

        // this way we break the cycle.
        if (!std::getline(std::cin, cmd_line)) {
            break;
        }

        smash.executeCommand(cmd_line.c_str());
    }

    return 0;
}
