//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef JOB_H
#define JOB_H

#include "string"


#include "command_proc.h"

class JobEntry
{
    // TODO: Add your data members
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

    std::string get_cmdLine() const
    {
        return cmd_line;
    }
};
#endif //JOB_H
