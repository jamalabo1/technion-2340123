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

    const int temp_stdout = dup(STDOUT_FILENO);
    if (temp_stdout == -1)
    {
        // close the file
        close(fd);
        throw SyscallException("dup");
    }

    // move the file into stdout so that we write into it instead
    if (dup2(fd, STDOUT_FILENO) == -1)
    {
        // close
        close(temp_stdout);
        close(fd);
        throw SyscallException("dup2");
    }

    // now the fd arrow is not required, because we have the stdout arrow
    if (close(fd) == -1)
    {
        // again restore stdout before throwing original
        dup2(temp_stdout, STDOUT_FILENO);
        close(temp_stdout);

        throw SyscallException("close");
    }

    try
    {
        // run the command
        cmd->run();
        //we are sure to return here since execv's run in a forked children
    }
    catch (...)
    {
        // not checking for errors because we want to report the original error
        // and if these fail then it's out of our control (maybe os refusing everything?)
        dup2(temp_stdout, STDOUT_FILENO);
        close(temp_stdout);

        throw;
    }

    //  return the original
    if (dup2(temp_stdout, STDOUT_FILENO) == -1)
    {
        throw SyscallException("dup2");
    }

    // close the file
    if (close(temp_stdout) == -1)
    {
        throw SyscallException("close");
    }
}
