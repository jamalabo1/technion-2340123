//
// Created by jamal on 20/05/2026.
//
#include "exceptions.h"
#include "macros.h"
#include "special.h"
#include <pwd.h>
#include <dirent.h>
#include <fcntl.h>
#include <queue>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fd.h"

using namespace std;

REGISTER_COMMAND(DiskUsageCommand);


void DiskUsageCommand::execute() {
    //TODO: must be able to run on bg

    auto dir = params()->singleOrDefault<CommandTooManyArguments>();

    if (dir.empty()) {
        // why not set this at the default value? because it's not lazy evaluation, and syscalls are costly
        dir = Shell::Utils::get_current_cwd();
    }

    long totalBytes = 0;

    queue<string> q;
    // starting with the given dir.
    q.push(dir);

    // keep calculating
    while (!q.empty()) {
        //current dir
        dir = q.front();

        int fd = open(dir.c_str(), O_RDONLY);
        if (fd == -1) throw SyscallException("open");

        // scoped fd for throws.
        ScopedFd scopedFd(fd);

        // honoring the intent of this function (stackoverflow -  25035691) although it does not matter since we don't continue errored commands
        q.pop();

        Shell::Utils::traverse_dir(fd, [&fd, &q, &totalBytes, &dir](const string &name) {
            // if not this or cd .. then:
            if (name != "..") {
                struct stat st{};
                if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
                    // maybe permission/deleted file/etc.
                    throw SyscallException("fstatat");
                }

                if (name != "." && S_ISDIR(st.st_mode)) {
                    // since order does not matter we'll add it for further processing.
                    // but we must make sure we're supplying the relative path since our cwd did not change and we don't want to change it anyway
                    q.push(dir + "/" + name);
                    // recurse into dir/name
                } else {
                    // it's either files or the current dir
                    totalBytes += st.st_blocks;
                }
            }
        });

        // start iterating the next directory
    }

    // total size is in bytes, and we want to convert it to kb so
    long totalKb = (totalBytes + 1) / 2;
    // and this 1/2 trick is the rounding up

    std::cout << "Total disk usage: " << totalKb << " KB" << std::endl;
}
