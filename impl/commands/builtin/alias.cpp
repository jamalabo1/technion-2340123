//
// Created by jamal on 16/05/2026.
//

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"

REGISTER_COMMAND(AliasCommand)

//TODO: move to exceptions
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

    if (!param.empty())
    {
        auto args = Shell::Utils::_split(param, "=");

        //TODO: validate alias
        alias = args[0];
        command = args[1];

        //TODO: check if no edge cases are to be present
        // remove the quotes
        command = command.substr(1, command.size() - 2);
        return;
    }
    listAll = true;
}

void AliasCommand::execute()
{
    auto& aliasService = ctx.get_services()->aliases;

    if (listAll)
    {
        const auto& aliases = aliasService->mapList();

        for (auto& alias : aliases)
        {
            std::cout << alias.first << "=" << alias.second << std::endl;
        }

        return;
    }

    if (!aliasService->addAlias(alias, command))
    {
        throw CommandErrorMessage(alias + " already exists or is a reserved command");
    }
}
