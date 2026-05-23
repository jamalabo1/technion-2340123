//
// Created by jamal on 15/05/2026.
//

#ifndef SERVICES_H
#define SERVICES_H

#include "services/aliases.h"
#include "services/job_table.h"
#include "services/proc_mng.h"
#include "services/child_tracker.h"

//kinda IoC replacement

// if the projects requires afterward, maybe make it pseudoIoC, where instead of ctx->services relationship
// commands ask for the service as ioc->[service] in the constructor (through global singleton)
// such that Services definition does not appear in the header.
class Services {
public:
    std::shared_ptr<JobTable> table;
    std::shared_ptr<CommandProcManagement> proc_management;
    std::shared_ptr<Aliases> aliases;
    std::shared_ptr<ChildTracker> children;


    explicit Services(
        const std::shared_ptr<JobTable> &table,
        const std::shared_ptr<CommandProcManagement> &proc_management,
        const std::shared_ptr<Aliases> &aliases,
        const std::shared_ptr<ChildTracker> &children
    )
        : table(table),
          proc_management(proc_management),
          aliases(aliases),
          children(children) {
    }
};

#endif //SERVICES_H
