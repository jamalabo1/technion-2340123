//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef IMPL_H
#define IMPL_H

#include "command.h"
#include "built_in.h"


class ExternalCommand : public Command {
public:
    ExternalCommand(const std::string& cmd_line);

    virtual ~ExternalCommand() {
    }

    void execute() override;
};


class RedirectionCommand : public Command {
    // TODO: Add your data members
public:
    explicit RedirectionCommand(const std::string& cmd_line);

    virtual ~RedirectionCommand() {
    }

    void execute() override;
};

class PipeCommand : public Command {
    // TODO: Add your data members
public:
    PipeCommand(const std::string& cmd_line);

    virtual ~PipeCommand() {
    }

    void execute() override;
};

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
