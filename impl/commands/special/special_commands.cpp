//
// Created by jamal on 20/05/2026.
//
#include <fcntl.h>
#include <iostream>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>

#include "exceptions.h"
#include "fd.h"
#include "macros.h"
#include "special.h"


using namespace std;

SpecialCommand::SpecialCommand(CommandCtx &ctx) : Command(ctx) {
}

void SpecialCommand::run() {
    // prepare on parent
    prepare();

    // if not background then run on parent noramlly
    if (!ctx.get_flags().isBackground) {
        execute();

        return;
    }

    // otherwise create child
    int child = services->children->fork();

    if (child == 0) {
        // execute
        execute();

        // after execute i can exit from here or give to the parent, but i'll exit either way
        exit(0);
    } else {
        // save proc
        setProc(child);
        // and dont wait for it, the shell's run will create a job for it
    }
}


REGISTER_COMMAND(WhoAmICommand);

void WhoAmICommand::execute() {
    // user id
    const int uid = getuid();
    if (uid == -1) {
        throw SyscallException("getuid");
    }

    // read the content
    const string passwd = Shell::Utils::read_all("/etc/passwd");

    vector<string> entries = Shell::Utils::_split(passwd, "\n");

    string username;
    string gid;
    string dir;

    for (const auto &entry: entries) {
        // don't normalize empty
        vector<string> fields = Shell::Utils::_split(entry, ":", false);
        // if this is the entry of this uid
        if (fields[2] == to_string(uid)) {
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

REGISTER_COMMAND(USBInfoCommand);


void USBInfoCommand::execute() {
    // read the directories entries
    string dir = "/sys/bus/usb/devices";

    int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
        // if does not exist then we'll return non found as required
        if (errno == ENOENT) {
            throw CommandErrorMessage("no USB devices found");
        }
        throw SyscallException("open");
    }

    // this way we ensure fd is closed on out of scope
    ScopedFd scopedDir(fd);

    vector<pair<int, string> > entries;

    Shell::Utils::traverse_dir(fd, [&fd, &dir, &entries](const string &name) {
        // skip these
        if (name == "." || name == "..") return;

        // per piazza @102,@104 keep as is.
        // // TODO: root hubs and interfaces?
        // if (name.find(':') != string::npos) return;
        // if (name.find("usb") != string::npos) return;

        // ok so usbs that are in the directory exist (duh) but we are not interseted in all of them
        // so instead, we check a dummy devnum
        string usbPath = dir + "/" + name;

        string devNumPath = usbPath + "/devnum";
        // according to man7 i can check using this in relation to fstat
        if (access(devNumPath.c_str(), F_OK) != 0) {
            // does not exist
            return;
        }
        // otherwise exists

        string devNum = Shell::Utils::read_line(devNumPath);

        try {
            int devNumInt = Shell::Utils::parseInt(devNum);

            entries.emplace_back(
                devNumInt,
                usbPath
            );
        } catch (const invalid_argument& e) {
            // skip if does not have a dev num
        }
    });


    // if no device was found
    if (entries.empty()) {
        throw CommandErrorMessage("no USB devices found");
    }


    // now we sort and print
    std::sort(entries.begin(), entries.end(), [](const pair<int, string> &a, const pair<int, string> &b) {
        // sort by dev num
        return a.first < b.first;
    });


    // ok so we need to check and return n/a otherwise
    // since we don't want to use try/catch because some legitimate throws exists (syscall)
    auto _read = [](const string &path) -> string {
        if (access(path.c_str(), F_OK | R_OK) == 0) {
            auto result = Shell::Utils::read_line(path);
            if (result.empty()) {
                return "N/A";
            }
            return result;
        }
        return "N/A";
        // else if syscall error or smth (most def a syscall) then throw as regular
    };

    for (const auto &entry: entries) {
        string usbPath = entry.second;

        string idVendor = _read(usbPath + "/idVendor");
        string idProduct = _read(usbPath + "/idProduct");
        string manufacturer = _read(usbPath + "/manufacturer");
        string product = _read(usbPath + "/product");
        string bMaxPower = _read(usbPath + "/bMaxPower");


        std::cout
                << "Device " << entry.first << ": ID "
                << idVendor << ":" << idProduct << " "
                << manufacturer << " " << product << " "
                << "MaxPower: " << bMaxPower << std::endl;
        // Device 1: ID 0781:5583 SanDisk Ultra Fit  MaxPower: 200mA
    }
}
