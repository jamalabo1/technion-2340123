//
// Created by jamal on 20/05/2026.
//

#ifndef SPECIAL_H
#define SPECIAL_H

#include "command.h"


class SpecialCommand : public Command {
public:
    SpecialCommand(CommandCtx &);

    void run() override;
};

// du proc command
class DiskUsageCommand : public SpecialCommand {
    DEFAULT_COMMAND_HELPER("du", DiskUsageCommand, SpecialCommand)

private:
    std::string dir;

public:
    void execute() override;

    void prepare() override;
};


DEFINE_COMMAND("whoami", WhoAmICommand, SpecialCommand);

DEFINE_COMMAND("usbinfo", USBInfoCommand, SpecialCommand);

#endif //SPECIAL_H
