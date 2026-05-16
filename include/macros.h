//
// Created by jamal on 15/05/2026.
//

#ifndef MACROS_H
#define MACROS_H

#include "command.h"
#include "command_factory.h"

#define FACTORY_REGISTER_HELPER(command_id, CMD) \
class FactoryRegister \
    {\
    public:\
    FactoryRegister()\
        {\
            auto factory = CommandFactory::getInstance();\
            factory->registerCommand(command_id, []( CommandCtx ctx)\
            {\
                return new CMD(ctx);\
            });\
        }\
    };\
static FactoryRegister factoryRegister;

#define DEFAULT_COMMAND_HELPER(command_id, CMD, type)\
private:\
    FACTORY_REGISTER_HELPER(command_id, CMD);\
public:\
    explicit CMD(CommandCtx& ctx) : type(ctx){}


#define DEFINE_COMMAND(command_id, CMD, type) class CMD : public type {\
private: \
            FACTORY_REGISTER_HELPER(command_id, CMD)\
public: \
    explicit CMD( CommandCtx& ctx) : type(ctx) { \
     } \
    void execute() override; \
};

#define REGISTER_COMMAND(CMD) CMD::FactoryRegister CMD::factoryRegister{};

#endif //MACROS_H
