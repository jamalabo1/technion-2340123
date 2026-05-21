//
// Created by jamal on 16/05/2026.
//

#ifndef PROC_MNG_H
#define PROC_MNG_H
#include <memory>

#include "command_proc.h"


class CommandProcManagement {
private:
    std::shared_ptr<CommandProc> current_fg_proc;

public:
    CommandProcManagement();

    void setFgProc(const std::shared_ptr<CommandProc>& proc);

    std::shared_ptr<CommandProc> getFgProc() const;

    void clearFgProc();


    // to centralize waiting for clearing
    void waitFgProc();

    // for grouping
    void setAndWaitFgProc(const std::shared_ptr<CommandProc> &proc);
};

#endif //PROC_MNG_H
