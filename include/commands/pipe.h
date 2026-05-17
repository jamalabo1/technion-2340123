//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#ifndef PIPE_H
#define PIPE_H

#include "command.h"

class PipeCommand : public Command {
private:
    Command* cmd1;
    Command* cmd2;

public:
    PipeCommand(CommandCtx& ctx, Command* cmd1, Command* cmd2);

    virtual ~PipeCommand() {
    }

    void execute() override;
};



#endif //PIPE_H
