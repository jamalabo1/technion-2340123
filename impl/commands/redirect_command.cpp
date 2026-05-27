//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#include <unistd.h>
#include <fcntl.h>

#include "exceptions.h"
#include "fd.h"
#include "redirect.h"
#include "scoped_dup.h"
#include "scoped_dup2.h"

using std::unique_ptr;

FileWrapperProxy::FileWrapperProxy(CommandCtx &ctx, unique_ptr<Command> cmd): Command(ctx),
                                                                              isAppend(false),
                                                                              cmd(std::move(cmd)) {
};


void FileWrapperProxy::prepare() {
    auto pList = params()->list();

    fileName = pList[0];
    isAppend = pList[1] == "append";
}

void FileWrapperProxy::execute() {
    // ok so we want to run the builtin commands on the parent, so we don't want to fork, and let the execute command handle forking if they want. all I have to do is
    // temp change the parent stdout into the file and then return it after execution.

    // disable running on the background (as the requirements)
    cmd->disableBg();

    // open the file
    int flags = O_WRONLY | O_CREAT | (isAppend ? O_APPEND : O_TRUNC);

    int fd = open(fileName.c_str(), flags, 0666);
    if (fd == -1) {
        throw SyscallException("open");
    }
    // if successful make it scoped
    ScopedFd scopedFd(fd);

    // duplicate the stdout
    ScopedDup scopedDup(STDOUT_FILENO);

    // this all is replaced with scoping
    // const int temp_stdout = dup(STDOUT_FILENO);
    // if (temp_stdout == -1) {
    //     // close the file
    //     close(fd);
    //     throw SyscallException("dup");
    // }

    // // move the file into stdout so that we write into it instead
    ScopedDup2 scopedDup2(fd, STDOUT_FILENO);
    // and at this point the fd arrow is not required because it's in the stdout so make it out of scope.
    // but the problem is also scopeddup and scopeddup2 life cycle is destroyed. and it must be done in order

    // if (dup2(fd, STDOUT_FILENO) == -1) {
    //     // close
    //     close(temp_stdout);
    //     close(fd);
    //     throw SyscallException("dup2");
    // }

    // now the fd arrow is not required, because we have the stdout arrow
    // if (close(fd) == -1) {
    //     // again restore stdout before throwing original
    //     dup2(temp_stdout, STDOUT_FILENO);
    //     close(temp_stdout);
    //
    //     throw SyscallException("close");
    // }

    // any exception already makes the scoped out of scope so we are good
    cmd->run();

    // and the return to original is already maintained by the lifecycles above if we can extend them.


    // try {
    //     // run the command
    //     cmd->run();
    //     //we are sure to return here since execv's run in a forked children
    // } catch (...) {
    //     // not checking for errors because we want to report the original error
    //     // and if these fail then it's out of our control (maybe os refusing everything?)
    //     dup2(temp_stdout, STDOUT_FILENO);
    //     close(temp_stdout);
    //
    //     throw;
    // }
    //
    // //  return the original
    // if (dup2(temp_stdout, STDOUT_FILENO) == -1) {
    //     throw SyscallException("dup2");
    // }
    //
    // // close the file
    // if (close(temp_stdout) == -1) {
    //     throw SyscallException("close");
    // }
}
