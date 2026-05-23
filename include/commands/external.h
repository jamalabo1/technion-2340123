//
// Created by jamal on 15/05/2026.
//

#ifndef EXTERNAL_H
#define EXTERNAL_H

#include "command.h"

class ExternalCommand : public Command
{
public:
    ExternalCommand(CommandCtx& ctx);

    virtual ~ExternalCommand()
    {
    }

    void execute() override;
};

#endif //EXTERNAL_H
