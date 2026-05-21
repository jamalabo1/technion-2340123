//
// Created by jamal on 20/05/2026.
//
#include <fcntl.h>
#include <iostream>
#include <pwd.h>
#include <unistd.h>

#include "exceptions.h"
#include "macros.h"
#include "commands/special.h"


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
