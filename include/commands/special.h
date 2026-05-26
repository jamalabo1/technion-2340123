//
// Created by jamal on 20/05/2026.
//

#ifndef SPECIAL_H
#define SPECIAL_H

#include "command.h"


class SpecialCommand : public Command
{
public:
    SpecialCommand(CommandCtx&);

    void run() override;
};

DEFINE_COMMAND("du", DiskUsageCommand, SpecialCommand);

DEFINE_COMMAND("whoami", WhoAmICommand, SpecialCommand);

DEFINE_COMMAND("usbinfo", USBInfoCommand, SpecialCommand);

#endif //SPECIAL_H
