//
// Created by jamal on 16/05/2026.
//

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"

REGISTER_COMMAND(AliasCommand)

class AliasInvalidFormatException : public CommandErrorMessage
{
public:
    AliasInvalidFormatException(): CommandErrorMessage("invalid alias format")
    {
    }
};

void AliasCommand::prepare()
{
    auto param = params()->concat();

    // no param
    if (param.empty())
    {
        listAll = true;
        return;
    }

    std::smatch match;
    // group 1: ([a-zA-Z0-9_]+) matches the alias name
    // group 2: ([^']*) matches the command inside the single quotes
    std::regex pattern("^([a-zA-Z0-9_]+)='([^']*)'$");

    if (!std::regex_match(param, match, pattern))
    {
        throw AliasInvalidFormatException();
    }

    alias = match[1].str();
    command = match[2].str(); // and it already excludes the quotes
}


void AliasCommand::execute()
{
    auto& aliasService = services->aliases;

    if (listAll)
    {
        const auto& aliases = aliasService->mapList();

        for (auto& alias : aliases)
        {
            std::cout << alias.first << "=" << "\'" << alias.second << "\'" << std::endl;
        }

        return;
    }

    if (!aliasService->addAlias(alias, command))
    {
        throw CommandErrorMessage(alias + " already exists or is a reserved command");
    }
}


REGISTER_COMMAND(UnAliasCommand)

void UnAliasCommand::execute()
{
    //Since no preparations are used, for compactness do the validation in here

    auto pList = params()->list();

    if (pList.empty())
    {
        throw CommandNotEnoughArguments();
    }

    auto aliasService = services->aliases;

    for (auto& alias : pList)
    {
        if (!aliasService->removeAlias(alias))
        {
            throw CommandErrorMessage(alias + " alias does not exist");
        }
    }
}
