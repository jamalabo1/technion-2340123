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

    // this guards the stdout by duplicating it into a temp fd, then dup2's fd into stdout and on finsh recovers.
    ScopedGuardedDup2 scopedDup2(fd, STDOUT_FILENO);

    // keping the old scopeddup + scoepddup2 actually is bad because it dup's the stdout
    // into fd, which now means we have to arrows into the stdout

    // any exception already makes the scoped out of scope so we are good
    cmd->run();
}
