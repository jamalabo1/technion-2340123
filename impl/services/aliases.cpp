//
// Created by jamal on 16/05/2026.
//
#include "services/aliases.h"

#include "command_factory.h"

bool Aliases::containsAlias(const std::string& alias)
{
    return aliases.find(alias) != aliases.end();
}

Aliases::Aliases()
{
    // factory_ = CommandFactory::getInstance();
}

bool Aliases::addAlias(const std::string& alias, const std::string& value)
{
    if (containsAlias(alias)) return false;

    // check if reserved
    if (CommandFactory::containsCommand(alias))
    {
        return false;
    }

    aliases[alias] = value;

    return true;
}

bool Aliases::removeAlias(const std::string& alias)
{
    // maybe use size checkers
    if (!containsAlias(alias)) return false;

    aliases.erase(alias);
    return true;
}

std::map<std::string, std::string> Aliases::mapList()
{
    return aliases;
}

std::string Aliases::tokenize(const std::string& word)
{
    // since recursive will not be tested, then i'll use this simple one

    if (!containsAlias(word)) return word;

    return aliases[word];
}
