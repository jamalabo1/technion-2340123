//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#include <unistd.h>
#include <fcntl.h>

#include "exceptions.h"
#include "commands/redirect.h"

void FileWrapperProxy::prepare() {
    auto pList = params()->list();

    fileName = pList[0];
    isAppend = pList[1] == "append";
}

void FileWrapperProxy::execute() {
    // we are just a proxy to open the file and let command write to it through its stdout (new fd).

    int proxy = services->children->fork();


    if (proxy == 0) {
        //child
         int flags = O_WRONLY | O_CREAT | (isAppend ? O_APPEND : O_TRUNC);

         int fd = open(fileName.c_str(), flags, 0666);

         if (fd == -1) {
            throw SyscallException("open");
        }

         if (dup2(fd, STDOUT_FILENO) == -1) {
            throw SyscallException("dup2");
        }

         // must be before execute since if it's execv we cannot close afterward.
        close(fd);

        cmd->execute();

        //if not execv
        exit(0);
    }

    // treat it as a proc
    proc = new CommandProc();
    proc->pId = proxy;


    // we can make it bg by a simple flag, and it already handles signals.
    services->proc_management->setAndWaitFgProc(proc);

     // throw CommandErrorMessage("exception");

    // int flags = isAppend ? O_APPEND : O_CREAT;
    //
    // int fd = open(fileName.c_str(), flags);
    //
    // if (fd == -1) {
    //     throw SyscallException("open");
    // }
    //
    //
    // // this way procA tried to write into our stdin, which is the file, so it ends up writing to the file itself.
    // if (dup2(fd, STDIN_FILENO) == -1) {
    //     close(fd);
    //     throw SyscallException("dup2");
    // }
    //since pipe creates a fork for this process dup2 does not change the shell's stdin so we are safe.
}