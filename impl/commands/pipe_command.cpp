//
// Created by Jamal Abo Mokh on 17/05/2026.
//

#include <unistd.h>
#include <sys/wait.h>

#include "exceptions.h"
#include "fd.h"
#include "pipe.h"
#include "scoped_dup2.h"

using std::unique_ptr;


PipeCommand::PipeCommand(CommandCtx &ctx, unique_ptr<Command> cmd1, unique_ptr<Command> cmd2) : Command(ctx),
    cmd1(std::move(cmd1)),
    cmd2(std::move(cmd2)) {
}

// TODO: expand on this later
#define GUARDED_SYSCALL(call, ...) if(call(__VA_ARGS__) == -1) throw SyscallException(#call);

#define CLOSE(...) GUARDED_SYSCALL(close, __VA_ARGS__)
#define DUP2(...) GUARDED_SYSCALL(dup2, __VA_ARGS__)

void PipeCommand::execute() {
    // create a pipe in the parent
    int fd[2];

    // pipe(fd)
    GUARDED_SYSCALL(pipe, fd);

    // disable bg per requirement, also saves us from re-wiring stdout
    cmd1->disableBg();
    cmd2->disableBg();

    int child1 = services->children->fork();

    if (child1 == 0) {
        // now change the fds for child1

        // for the child1 perspective, it's only going to write, so close the read arrow
        CLOSE(fd[0]);

        // primitive but effective.
        int wireChannel = (params()->firstOrDefault() == "&") ? STDERR_FILENO : STDOUT_FILENO;

        // this way we ensure whatever happens we try to restore
        ScopedDup2 scopedPipeWrite(fd[1], wireChannel);

        CLOSE(fd[1]);

        // execute as regular. (we don't mind if it waits for  execv's or not, we have our own child wrapper)
        cmd1->run();

        // but if not excev, then quit this instance
        exit(0);

        return;
    }

    int child2 = services->children->fork();

    if (child2 == 0) {
        // child2 does not writes to the pipe so
        CLOSE(fd[1]);

        // this way we ensure whatever happens we try to restore
        ScopedDup2 scopedPipeWrite(fd[0], STDIN_FILENO);

        //then close the pipe original place
        CLOSE(fd[0]);

        // change for child2
        cmd2->run();

        // and here exit
        exit(0);

        return;
    }

    // and the parent does not write nor read
    CLOSE(fd[0]);
    CLOSE(fd[1]);

    // now on the parent side, we can turn this into a job through the fg process by introducing another fork that waits for these two
    // but since all pipes are foreground, we might as well just wait regularly.

    //something like:
    /*
     * int waitable = fork()
     * if(waitable == 0) waitpid(child1); waitpid(child2);
     * proc = new CommandProc(); proc->pId = waitable;
     * proc_mng->setAndWait(proc);
     */

    //perform a blocking wait in the parent for both
    if (waitpid(child1, nullptr, 0) == -1) {
        throw SyscallException("waitpid");
    };
    if (waitpid(child2, nullptr, 0) == -1) {
        throw SyscallException("waitpid");
    };
}
