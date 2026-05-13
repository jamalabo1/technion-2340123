//
// Created by Jamal Abo Mokh on 13/05/2026.
//
#include "commands/built_in.h"

void ChangeTerminalNameCommand::execute() {
    // take the ref of the working env
    auto& wenv = ctx.get_wenv();

    auto termName = ctx.get_params()->single<std::exception>();

    wenv.setTerminalName(termName);
}



