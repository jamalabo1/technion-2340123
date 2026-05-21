#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "shell.h"

#include "signals.h"


int main(int argc, char* argv[]) {
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR)
    {
        perror("smash error: failed to set ctrl-C handler");
    }

    SmallShell& smash = SmallShell::getInstance();



    while (true)
    {
        std::string termName = smash.get_terminalName();
        std::cout << termName << "> ";

        std::string cmd_line;


        if (!std::getline(std::cin, cmd_line)) {
             //TODO: wtf?
            if (std::cin.eof()) {
                std::cerr << "\nstdin EOF; exiting shell loop in " << getpid() << std::endl;;
            } else {
                std::cerr << "\nstdin error; exiting shell loop\n";

            }
            break;
        }

        smash.executeCommand(cmd_line.c_str());
    }

    return 0;
}
