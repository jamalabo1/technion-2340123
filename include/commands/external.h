//
// Created by jamal on 15/05/2026.
//

#ifndef EXTERNAL_H
#define EXTERNAL_H

#include "command.h"

class ExternalBehaviour
{
};

class SimpleExternalBehaviour : ExternalBehaviour
{
};

class ComplexExternalBehaviour : ExternalBehaviour
{
};

class ExternalCommand : public Command
{
private:
    ExternalBehaviour* behaviour;


public:
    ExternalCommand(CommandCtx& ctx);

    virtual ~ExternalCommand()
    {
    }

    void execute() override;
};

#endif //EXTERNAL_H
