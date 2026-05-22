//
// Created by jamal on 20/05/2026.
//

#ifndef SPECIAL_H
#define SPECIAL_H

#include "command.h"

DEFINE_COMMAND("du", DiskUsageCommand, Command);

DEFINE_COMMAND("whoami", WhoAmICommand, Command);

DEFINE_COMMAND("usbinfo", USBInfoCommand, Command);

#endif //SPECIAL_H
