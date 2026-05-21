//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_H
#define JOB_H

#include <iostream>
#include <utility>

#include "string"


#include "command_proc.h"

class JobEntry
{
private:
    std::shared_ptr<CommandProc> proc;

    std::string cmd_line;

    int jobId;

public:
    JobEntry(
        const int job_id,
        const std::shared_ptr<CommandProc> &proc,
        std::string cmd_line
    )
        : proc(proc),
          cmd_line(std::move(cmd_line)),
          jobId(job_id)
    {
    }

    int get_id() const
    {
        return jobId;
    }

    int get_pId() const
    {
        return proc->pId;
    }

    std::shared_ptr<CommandProc> get_proc() const
    {
        return proc;
    }

    void printCmdWithPid() const
    {
        std::cout << cmd_line << " " << proc->pId << std::endl;
    }
    // print without end line
    void printCmdLine() const
    {
        std::cout << cmd_line;
    }
};
#endif //JOB_H
