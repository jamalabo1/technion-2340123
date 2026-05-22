//
// Created by jamal on 20/05/2026.
//
#include "exceptions.h"
#include "macros.h"
#include "commands/special.h"
#include <pwd.h>
#include <dirent.h>
#include <fcntl.h>
#include <queue>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

REGISTER_COMMAND(DiskUsageCommand);

void DiskUsageCommand::execute()
{
    auto dir = params()->singleOrDefault<CommandTooManyArguments>();
    if (dir.empty())
    {
        // why not set this at the default value? because it's not lazy evaluation, and syscalls are costly
        dir = Shell::Utils::get_current_cwd();
    }

    // constexpr size_t BUF_SIZE = 8192;
    // char buf[BUF_SIZE];

    long totalBytes = 0;

    queue<string> q;
    // starting with the given dir.
    q.push(dir);

    // keep calculating
    while (!q.empty())
    {
        string dir = q.front();
        int fd = open(dir.c_str(), O_RDONLY);
        if (fd == -1) throw SyscallException("open");

        // honoring the intent of this function (stackoverflow -  25035691) although it does not matter since we don't continue errored commands
        q.pop();

        // for this specific directory we don't know the count so were gonna keep running through them
        // while (true)
        // {
        // int dents = getdents64(fd, buf, BUF_SIZE);
        // if (dents == -1)
        // {
        //     // close and report
        //     close(fd);
        //     throw SyscallException("getdents64");
        // }
        //
        // if (dents == 0)
        //     // we finished this directory
        //     break;
        //
        // for (int bpos = 0; bpos < dents;)
        // {
        //     auto* d = reinterpret_cast<linux_dirent64*>(buf + bpos);
        //
        //     string name = d->d_name;
        //
        //     // if not this or cd .. then:
        //     if (name != "..")
        //     {
        //         struct stat st{};
        //         if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1)
        //         {
        //             // close and report
        //             close(fd);
        //             // maybe permission/deleted file/etc.
        //             throw SyscallException("fstatat");
        //         }
        //
        //
        //         if (name != "." && S_ISDIR(st.st_mode))
        //         {
        //             //since order does not matter we'll add it for further processing.
        //             // but we must make sure we're supplying the relative path since our cwd did not change and we don't want to change it anyway
        //             q.push(dir + "/" + name);
        //             // recurse into dir/name
        //         }
        //         else
        //         {
        //             // it's either files or the current dir
        //             totalBytes += st.st_blocks;
        //         }
        //     }
        //
        //     bpos += d->d_reclen;
        // }

        Shell::Utils::traverse_dir(fd, [&fd, &q, &totalBytes, &dir](const string& name)
        {
            // if not this or cd .. then:
            if (name != "..")
            {
                struct stat st{};
                if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1)
                {
                    // close and report
                    close(fd);
                    // maybe permission/deleted file/etc.
                    throw SyscallException("fstatat");
                }


                if (name != "." && S_ISDIR(st.st_mode))
                {
                    //since order does not matter we'll add it for further processing.
                    // but we must make sure we're supplying the relative path since our cwd did not change and we don't want to change it anyway
                    q.push(dir + "/" + name);
                    // recurse into dir/name
                }
                else
                {
                    // it's either files or the current dir
                    totalBytes += st.st_blocks;
                }
            }
        });

        //     if (!hasMore)
        //     {
        //         // we finished this directory
        //         break;
        //     }
        // }
        // start iterating the next directory
        close(fd);
    }

    // total size is in bytes, and we want to convert it to kb so
    long totalKb = (totalBytes + 1) / 2;
    // and this 1/2 trick is the rounding up

    std::cout << "Total disk usage: " << totalKb << " KB" << std::endl;
}
