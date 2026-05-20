//
// Created by jamal on 20/05/2026.
//
#include <fcntl.h>
#include <unistd.h>

#include "exceptions.h"
#include "macros.h"
#include "commands/special.h"
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

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

    int fd;
    char d_type;

    constexpr size_t BUF_SIZE = 8192;
    char buf[BUF_SIZE];

    int totalSize = 0;

    // keep calculating
    while (true) {
        int fd = open(dir.c_str(), O_RDONLY);
        if (fd == -1) throw SyscallException("open");


        // for this specfic directory we don't know the count so we gonna keep runing through them
        while (true) {
            // nread = syscall(SYS_getdents, fd, buf, BUF_SIZE);
            // ik now sizeof(char)=1, but to make it clear.
            int dents = getdents64(fd, buf, BUF_SIZE);

            if (dents == -1)
                throw SyscallException("getdents64");

            if (dents == 0)
                // we finished this directory
                break;

            for (int bpos = 0; bpos < dents;) {
                auto *d = reinterpret_cast<linux_dirent64 *>(buf + bpos);

                std::string name = d->d_name;

                // if not this or cd .. then:
                if (name != ".." && name != ".") {
                    struct stat st{};
                    if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
                        // maybe permission/deleted file/etc.
                        throw SyscallException("fstatat");
                    }

                    totalSize += st.st_size;

                    if (S_ISDIR(st.st_mode)) {
                        // recurse into dir/name
                        std::cout << "dir exists at " << name << std::endl;
                    }
                }

                bpos += d->d_reclen;
            }
        }
        // start iterating the next directory
        break;
    }

    std::cout << "Total size: " << totalSize << std::endl;
}

REGISTER_COMMAND(WhoAmICommand);

void WhoAmICommand::execute() {
    // user id
    uid_t uid = getuid();
    if (uid == -1) {
        throw SyscallException("getuid");
    }

    // then call the getpwuid which has username and group id
    struct passwd *p = getpwuid(uid);

    // user name
    std::cout << p->pw_name << std::endl;
    // user id
    std::cout << uid << std::endl;
    //group id
    std::cout << p->pw_gid << std::endl;
    //home dir id
    std::cout << p->pw_dir << std::endl;
}
