//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef BUILT_IN_H
#define BUILT_IN_H

#include "command.h"
#include "macros.h"

class BuiltInCommand : public Command
{
public:
    BuiltInCommand(CommandCtx& ctx) : Command(ctx)
    {
    };

    virtual ~BuiltInCommand()
    {
    }
};


// ChangeTerminalNameCommand
DEFINE_COMMAND("chprompt", ChangeTerminalNameCommand, BuiltInCommand);

// Quit Command
DEFINE_COMMAND("quit", QuitCommand, BuiltInCommand);


// kill proc command
class KillCommand : public BuiltInCommand
{
    DEFAULT_COMMAND_HELPER("kill", KillCommand, BuiltInCommand)

private:
    int sigNum = -1;
    int jobId = -1;

public:
    void execute() override;

    void prepare() override;
};


// jobs command
DEFINE_COMMAND("jobs", JobsCommand, BuiltInCommand);

// Alias proc command
class AliasCommand : public BuiltInCommand
{
    DEFAULT_COMMAND_HELPER("alias", AliasCommand, BuiltInCommand)

private:
    bool listAll;
    std::string alias;
    std::string command;

public:
    void execute() override;

    void prepare() override;
};


class ChangeDirCommand : public BuiltInCommand
{
    // TODO: Add your data members public:
    ChangeDirCommand(const CommandCtx& ctx);

    virtual ~ChangeDirCommand()
    {
    }

    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand
{
public:
    GetCurrDirCommand(const CommandCtx& ctx);

    virtual ~GetCurrDirCommand()
    {
    }

    void execute() override;
};

class ShowPidCommand : public BuiltInCommand
{
public:
    ShowPidCommand(const CommandCtx& ctx);

    virtual ~ShowPidCommand()
    {
    }

    void execute() override;
};


// bring to fg command
class ForegroundCommand : public BuiltInCommand
{
    DEFAULT_COMMAND_HELPER("fg", ForegroundCommand, BuiltInCommand)

private:
    int jobId = -1;

public:
    void execute() override;

    void prepare() override;
};


class UnAliasCommand : public BuiltInCommand
{
public:
    UnAliasCommand(const CommandCtx& ctx);

    virtual ~UnAliasCommand()
    {
    }

    void execute() override;
};

class UnSetEnvCommand : public BuiltInCommand
{
public:
    UnSetEnvCommand(const CommandCtx& ctx);

    virtual ~UnSetEnvCommand()
    {
    }

    void execute() override;
};

class SysInfoCommand : public BuiltInCommand
{
public:
    SysInfoCommand(const CommandCtx& ctx);

    virtual ~SysInfoCommand()
    {
    }

    void execute() override;
};

#endif //BUILT_IN_H
