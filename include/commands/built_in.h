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

class QuitCommand : public BuiltInCommand
{
    // TODO: Add your data members public:
    QuitCommand(const CommandCtx& ctx);

    virtual ~QuitCommand()
    {
    }

    void execute() override;
};



DEFINE_COMMAND("jobs", JobsCommand, BuiltInCommand);




class KillCommand : public BuiltInCommand
{
    // TODO: Add your data members
public:
    KillCommand(const CommandCtx& ctx);

    virtual ~KillCommand()
    {
    }

    void execute() override;
};

class ForegroundCommand : public BuiltInCommand
{
    // TODO: Add your data members
public:
    ForegroundCommand(const CommandCtx& ctx);

    virtual ~ForegroundCommand()
    {
    }

    void execute() override;
};

class AliasCommand : public BuiltInCommand
{
public:
    AliasCommand(const CommandCtx& ctx);

    virtual ~AliasCommand()
    {
    }

    void execute() override;
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
