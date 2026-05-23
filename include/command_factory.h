//
// Created by jamal on 15/05/2026.
//

#ifndef COMMAND_FACTORY_H
#define COMMAND_FACTORY_H

#include "command.h"

#include <map>
#include <functional>

class CommandFactory {
private:
    using FactoryInstanceCreatorType = std::function<Command*(CommandCtx)>;

    std::unordered_map<std::string, FactoryInstanceCreatorType> table;

    bool _containsCommand(const std::string &key) {
        return table.find(key) != table.end();
    }

public:
    Command *create(
        std::string key,
        const CommandCtx &ctx
    );

    void registerCommand(const std::string &id, FactoryInstanceCreatorType func);


    static bool containsCommand(const std::string &key);

    static std::shared_ptr<CommandFactory> getInstance();
};

#endif //COMMAND_FACTORY_H
