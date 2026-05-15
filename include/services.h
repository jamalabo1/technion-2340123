//
// Created by jamal on 15/05/2026.
//

#ifndef SERVICES_H
#define SERVICES_H

#include "job_table.h"

class Services
{
public:
    std::shared_ptr<JobTable> table;


    explicit Services(const std::shared_ptr<JobTable>& table)
        : table(table)
    {
    }
};

#endif //SERVICES_H
