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
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

struct linux_dirent64 {
    ino64_t d_ino; /* 64-bit inode number */
    off64_t d_off; /* Not an offset; see getdents() */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type; /* File type */
    char d_name[]; /* Filename (null-terminated) */
};

REGISTER_COMMAND(DiskUsageCommand);

void DiskUsageCommand::execute() {
    auto dir = params()->single<CommandTooManyArguments>();

    constexpr size_t BUF_SIZE = 8192;
    char buf[BUF_SIZE];

    int totalSize = 0;

    queue<string> q;
    // starting with the given dir.
    q.push(dir);

    // keep calculating
    while (!q.empty()) {
        string dir = q.front();
        int fd = open(dir.c_str(), O_RDONLY);
        if (fd == -1) throw SyscallException("open");

        // honoring the intent of this function (stackoverflow -  25035691) although it does not matter since we don't continue errored commands
        q.pop();

        // for this specific directory we don't know the count so were gonna keep running through them
        while (true) {
            int dents = getdents64(fd, buf, BUF_SIZE);
            if (dents == -1)
                throw SyscallException("getdents64");

            if (dents == 0)
                // we finished this directory
                    break;

            for (int bpos = 0; bpos < dents;) {
                auto *d = reinterpret_cast<linux_dirent64 *>(buf + bpos);

                string name = d->d_name;

                // if not this or cd .. then:
                if (name != ".." && name != ".") {
                    struct stat st{};
                    if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
                        // maybe permission/deleted file/etc.
                        throw SyscallException("fstatat");
                    }


                    if (S_ISDIR(st.st_mode)) {
                        //since order does not matter we'll add it for further processing.
                        // but we must make sure we're supplying the relative path since our cwd did not change and we don't want to change it anyway
                        q.push(dir + "/" + name);
                        // recurse into dir/name
                    } else {

                        totalSize += st.st_size;
                    }
                }

                bpos += d->d_reclen;
            }
        }
        // start iterating the next directory
    }

    // total size is in bytes, and we want to convert it to kb so
    totalSize = totalSize / 1000;

    std::cout << "Total size: " << totalSize << " KB"  << std::endl;
}
