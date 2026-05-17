//
// Created by jamal on 16/05/2026.
//

#ifndef ALIASES_H
#define ALIASES_H
#include <map>
#include <string>

// #include "command_factory.h"

class Aliases
{
private:
    std::map<std::string, std::string> aliases;

    // std::shared_ptr<CommandFactory> factory_;

    bool containsAlias(const std::string& alias);

public:
    Aliases();

    bool addAlias(const std::string& alias, const std::string& value);

    bool removeAlias(const std::string& alias);

    std::map<std::string, std::string> mapList();

    std::string tokenize(const std::string& word);
};

#endif //ALIASES_H
