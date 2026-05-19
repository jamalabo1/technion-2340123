//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#ifndef REDIRECT_H
#define REDIRECT_H

#include "command.h"

// I/O Redirection command
class RedirectionCommand : public Command {
private:

public:
    void execute() override;

    void prepare() override;
};


// to be efficent and remove redundency, i'll treat i/o redirection as special pipe commands
// where cmd2 will be the writer, so we need a custom unregistered command
class FileWrapperProxy : public Command {
private:
    std::string fileName;
    bool isAppend;
    Command *cmd;

public:
    explicit FileWrapperProxy(CommandCtx &ctx, Command *cmd): Command(ctx),
                                                              cmd(cmd) {
    };

    void prepare() override;

    void execute() override;
};


#endif //REDIRECT_H
