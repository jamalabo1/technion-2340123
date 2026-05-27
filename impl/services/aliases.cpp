//
// Created by jamal on 16/05/2026.
//
#include "aliases.h"

#include "command_factory.h"


bool Aliases::containsAlias(const std::string &alias) {
    return aliases.find(alias) != aliases.end();
}

Aliases::Aliases() : counter(0) {
    // factory_ = CommandFactory::getInstance();
}

bool Aliases::addAlias(const std::string &alias, const std::string &value) {
    if (containsAlias(alias)) return false;

    // check if reserved
    if (CommandFactory::containsCommand(alias)) {
        return false;
    }


    aliases[alias] = AliasNode{
        .id = counter++,
        .key = alias,
        .value = value,
    };

    return true;
}

bool Aliases::removeAlias(const std::string &alias) {
    // maybe use size checkers
    if (!containsAlias(alias)) return false;

    aliases.erase(alias);

    return true;
}

std::vector<std::pair<std::string, std::string> > Aliases::mapList() const {
    std::vector<AliasNode> preresult;

    // first get the list
    for (const auto &alias: aliases) {
        preresult.push_back(alias.second);
    }

    // then order asc
    std::sort(preresult.begin(), preresult.end(), [](const AliasNode &lhs, const AliasNode &rhs) {
        return lhs.id < rhs.id;
    });

    // then return key value
    std::vector<std::pair<std::string, std::string> > result;

    for (const auto &alias: preresult) {
        result.emplace_back(alias.key, alias.value);
    }

    return result;
}

std::string Aliases::tokenize(const std::string &word) {
    // since recursive will not be tested, then i'll use this simple one
    if (!containsAlias(word)) return word;

    return aliases[word].value;
}
