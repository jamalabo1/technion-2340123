//
// Created by jamal on 15/05/2026.
//

#ifndef SERVICES_H
#define SERVICES_H

#include "aliases.h"
#include "job_table.h"
#include "services/proc_mng.h"

//kinda IoC replacement

class Services
{
public:
    std::shared_ptr<JobTable> table;
    std::shared_ptr<CommandProcManagement> proc_management;
    std::shared_ptr<Aliases> aliases;


    explicit Services(
        const std::shared_ptr<JobTable>& table,
        const std::shared_ptr<CommandProcManagement>& proc_management,
        const std::shared_ptr<Aliases>& aliases
    )
        : table(table),
          proc_management(proc_management),
          aliases(aliases)
    {
    }
};

#endif //SERVICES_H
