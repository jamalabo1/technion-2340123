//
// Created by jamal on 16/05/2026.
//

#ifndef ALIASES_H
#define ALIASES_H
#include <map>
#include <string>
#include <vector>

// #include "command_factory.h"

class Aliases
{
private:
    struct AliasNode
    {
        int id;
        std::string key;
        std::string value;
    };

    std::map<std::string, AliasNode> aliases;

    int counter;
    // I could use double AVLs for to save my self the ordering, but come on.

    bool containsAlias(const std::string& alias);

public:
    Aliases();

    bool addAlias(const std::string& alias, const std::string& value);

    bool removeAlias(const std::string& alias);

    // <key, alias> ordered by key ASC
    std::vector<std::pair<std::string, std::string>> mapList();

    std::string tokenize(const std::string& word);
};

#endif //ALIASES_H
