//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#ifndef PIPE_H
#define PIPE_H

#include "command.h"

class PipeCommand : public Command {
private:
    // Command* cmd1;
    std::unique_ptr<Command> cmd1;
    std::unique_ptr<Command> cmd2;

public:
    PipeCommand(CommandCtx& ctx, std::unique_ptr<Command> cmd1, std::unique_ptr<Command> cmd2);

    void execute() override;
};



#endif //PIPE_H
