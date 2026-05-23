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
    CommandFactory() {
    };

    Command* create(
        std::string key,
        const CommandCtx& ctx
    ) {
        if (table.find(key) == table.end()) {
            return nullptr;
        }

        return table[key](ctx);
    }

    void registerCommand(const std::string &id, FactoryInstanceCreatorType func) {
        table[id] = std::move(func);
    }


    static bool containsCommand(const std::string &key) {
        return getInstance()->_containsCommand(key);
    }

    static std::shared_ptr<CommandFactory> getInstance() {
        static std::shared_ptr<CommandFactory> instance = std::make_shared<CommandFactory>();

        return instance;
    }
};

#endif //COMMAND_FACTORY_H
