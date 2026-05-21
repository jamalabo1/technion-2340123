//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef COMMAND_H
#define COMMAND_H

#include "context.h"
#include "command_proc.h"


class Command
{
protected:
    //ctx
    CommandCtx ctx;

    // running proc of this command
    std::shared_ptr<CommandProc> proc;

    const Parameters* params() const
    {
        return &ctx.get_params();
    }

    void setProc(int pid)
    {
        proc = std::make_shared<CommandProc>();
        proc->pId = pid;
    }

    std::shared_ptr<Services> services;

public:
    explicit Command(CommandCtx& ctx): ctx(ctx), proc(nullptr)
    {
        services = ctx.get_services();
    };

    virtual ~Command() = default;

    virtual void execute() = 0;

    virtual CommandCtx& get_ctx()
    {
        return ctx;
    }

    std::shared_ptr<CommandProc> get_proc();

    virtual void prepare()
    {
        // for validations mainly
    };

    // a unifed call
    void run()
    {
        prepare();
        execute();
    }

    virtual void disableBg()
    {
        ctx.get_flags().isBackground = false;
    }

    //virtual void cleanup();
    // TODO: Add your extra methods if needed
};

#endif //COMMAND_H
