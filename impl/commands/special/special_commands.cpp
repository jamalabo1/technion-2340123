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


using namespace std;

REGISTER_COMMAND(WhoAmICommand);

void WhoAmICommand::execute()
{
    // user id
    const uid_t uid = getuid();
    if (uid == -1)
    {
        throw SyscallException("getuid");
    }

    // read the content
    const string passwd = Shell::Utils::read_all("/etc/passwd");

    vector<string> entries = Shell::Utils::_split(passwd, "\n");

    string username;
    string gid;
    string dir;

    for (const auto& entry : entries)
    {
        vector<string> fields = Shell::Utils::_split(entry, ":");
        // if this is the entry of this uid
        if (fields[2] == to_string(uid))
        {
            // according to man7 the format is
            // name:password:UID:GID:GECOS:directory:shell

            username = fields[0];
            gid = fields[3];
            dir = fields[5];
        }
    }

    //user name
    std::cout << username << std::endl;
    // user id
    std::cout << uid << std::endl;
    // group id
    std::cout << gid << std::endl;
    // home dir id
    std::cout << dir << std::endl;
}
