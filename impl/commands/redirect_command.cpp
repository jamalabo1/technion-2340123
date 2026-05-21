//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#include <unistd.h>
#include <fcntl.h>

#include "exceptions.h"
#include "commands/redirect.h"

void FileWrapperProxy::prepare()
{
    auto pList = params()->list();

    fileName = pList[0];
    isAppend = pList[1] == "append";
}

void FileWrapperProxy::execute()
{
    // // we are just a proxy to open the file and let command write to it through its stdout (new fd).
    //
    // int proxy = services->children->fork();
    //
    //
    // if (proxy == 0) {
    //     //child
    //     int flags = O_WRONLY | O_CREAT | (isAppend ? O_APPEND : O_TRUNC);
    //
    //     int fd = open(fileName.c_str(), flags, 0666);
    //
    //     if (fd == -1) {
    //         throw SyscallException("open");
    //     }
    //
    //     if (dup2(fd, STDOUT_FILENO) == -1) {
    //         throw SyscallException("dup2");
    //     }
    //
    //     // must be before execute since if it's execv we cannot close afterward.
    //     close(fd);
    //
    //     // but also prepare
    //     cmd->run();
    //
    //     //if not execv
    //     exit(0);
    // }
    //
    // // treat it as a proc
    // setProc(proxy);
    //
    // // we can make it bg by a simple flag, and it already handles signals.
    // services->proc_management->setAndWaitFgProc(proc);

    // ok so we want to run the builtin commands on the parent, so we don't want to fork, and let the execute command handle forking if they want. all I have to do is
    // temp change the parent stdout into the file and then return it after execution.

    // disable running on the background (as the requirements)
    cmd->disableBg();

    // open the file
    int flags = O_WRONLY | O_CREAT | (isAppend ? O_APPEND : O_TRUNC);

    int fd = open(fileName.c_str(), flags, 0666);
    if (fd == -1)
    {
        throw SyscallException("open");
    }

    int temp_stdout = dup(STDOUT_FILENO);
    if (temp_stdout == -1)
    {
        throw SyscallException("dup");
    }

    // move the file into stdout so that we write into it instead
    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        throw SyscallException("dup2");
    }

    // run the command
    cmd->run();

    // close the file
    if (close(fd) == -1)
    {
        throw SyscallException("close");
    }

    // then return the original
    if (dup2(temp_stdout, STDOUT_FILENO) == -1)
    {
        throw SyscallException("dup2");
    }


}
