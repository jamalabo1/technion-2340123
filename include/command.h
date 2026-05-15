//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef COMMAND_H
#define COMMAND_H

#include <utility>

#include "context.h"
#include "command_proc.h"


class Command
{
    // TODO: Add your data members
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
    explicit Command(CommandCtx& ctx): ctx(ctx)
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

    //virtual void prepare();
    //virtual void cleanup();
    // TODO: Add your extra methods if needed
};

#endif //COMMAND_H
