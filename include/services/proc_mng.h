//
// Created by jamal on 16/05/2026.
//

#ifndef PROC_MNG_H
#define PROC_MNG_H
#include "command_proc.h"


class CommandProcManagement {
private:
    CommandProc *current_fg_proc;

public:
    CommandProcManagement();


    void setFgProc(CommandProc *proc);

    CommandProc *getFgProc() const;

    void clearFgProc();


    // to centralize waiting for clearing
    void waitFgProc();

    // for grouping
    void setAndWaitFgProc(CommandProc *proc);
};

#endif //PROC_MNG_H
