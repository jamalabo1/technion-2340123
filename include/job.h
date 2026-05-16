//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_H
#define JOB_H

#include <iostream>

#include "string"


#include "command_proc.h"

class JobEntry
{
private:
    CommandProc* proc;

    std::string cmd_line;

    int jobId;

public:
    JobEntry(
        int job_id,
        CommandProc* proc,
        std::string cmd_line
    )
        : proc(proc),
          cmd_line(cmd_line),
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

    CommandProc* get_proc() const
    {
        return proc;
    }

    // print without endline
    void printCmdLine() const
    {
        std::cout << cmd_line;
    }
};
#endif //JOB_H
