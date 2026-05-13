//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef COMMAND_H
#define COMMAND_H
#include <utility>

#include "context.h"

class Command {
    // TODO: Add your data members
protected:
    CommandCtx ctx;

public:
    explicit Command(CommandCtx ctx): ctx(std::move(ctx)) {};

    virtual ~Command(){};

    virtual void execute() = 0;

    //virtual void prepare();
    //virtual void cleanup();
    // TODO: Add your extra methods if needed
};


#endif //COMMAND_H
