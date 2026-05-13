// Ver: 04-11-2025
#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <vector>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)

#include "wenv.h"
#include "commands/all.h"

class SmallShell {
private:
    // TODO: Add your data members
    SmallShell();

    // default values here
    WorkingEnvironment wenv = WorkingEnvironment(
        "smash"
    );

public:
    Command *CreateCommand(const char *cmd_line);

    SmallShell(SmallShell const &) = delete; // disable copy ctor
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance() // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    ~SmallShell();

    void executeCommand(const char *cmd_line);

    WorkingEnvironment &get_wenv() { return wenv; }

    // TODO: add extra methods as needed
};

#endif //SMASH_COMMAND_H_
