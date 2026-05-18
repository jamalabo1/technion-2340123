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
    CommandProc* proc;

    const Parameters* params() const
    {
        return ctx.get_params();
    }

public:
    explicit Command(CommandCtx& ctx): ctx(ctx), proc(nullptr)
    {
    };

    virtual ~Command()
    {
    };

    virtual void execute() = 0;

    virtual CommandCtx& get_ctx()
    {
        return ctx;
    }

    CommandProc* get_proc();

    virtual void prepare()
    {
        // for validations mainly
    };
    //virtual void cleanup();
    // TODO: Add your extra methods if needed
};

#endif //COMMAND_H
