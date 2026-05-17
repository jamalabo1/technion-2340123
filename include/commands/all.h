//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef IMPL_H
#define IMPL_H

#include "command.h"
#include "built_in.h"
#include "external.h"





class DiskUsageCommand : public Command {
public:
    DiskUsageCommand(const std::string& cmd_line);

    virtual ~DiskUsageCommand() {
    }

    void execute() override;
};

class WhoAmICommand : public Command {
public:
    WhoAmICommand(const std::string& cmd_line);

    virtual ~WhoAmICommand() {
    }

    void execute() override;
};

class USBInfoCommand : public Command {
    // TODO: Add your data members **BONUS: 10 Points**
public:
    USBInfoCommand(const char* cmd_line);

    virtual ~USBInfoCommand() {
    }

    void execute() override;
};



#endif //IMPL_H
