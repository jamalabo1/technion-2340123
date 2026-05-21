//
// Created by Jamal Abo Mokh on 15/05/2026.
//
#include "command.h"

std::shared_ptr<CommandProc> Command::get_proc()
{
    return proc;
}
