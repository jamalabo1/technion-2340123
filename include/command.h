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

    virtual CommandCtx& get_ctx() {
        return ctx;
    }

    //virtual void prepare();
    //virtual void cleanup();
    // TODO: Add your extra methods if needed
};

#define DEFINE_COMMAND(command_id, CMD, type) class CMD : public type {\
public: \
    explicit CMD(const CommandCtx& ctx) : type(ctx) { \
    }\
    void execute() override; \
};


#endif //COMMAND_H
