//
// Created by jamal on 15/05/2026.
//

#ifndef EXTERNAL_H
#define EXTERNAL_H

#include "command.h"

class ExternalCommand : public Command
{
public:
    explicit ExternalCommand(CommandCtx& ctx);


    void execute() override;

    bool is_external() override;
};

#endif //EXTERNAL_H
