//
// Created by jamal on 23/05/2026.
//
#include "command_factory.h"

Command * CommandFactory::create(std::string key, const CommandCtx &ctx)  {
    if (table.find(key) == table.end()) {
        return nullptr;
    }

    return table[key](ctx);
}


void CommandFactory::registerCommand(const std::string &id, FactoryInstanceCreatorType func) {
    table[id] = std::move(func);
}

bool CommandFactory::containsCommand(const std::string &key)  {
    return getInstance()->_containsCommand(key);
}

std::shared_ptr<CommandFactory> CommandFactory::getInstance()  {
    static std::shared_ptr<CommandFactory> instance = std::make_shared<CommandFactory>();

    return instance;
}
