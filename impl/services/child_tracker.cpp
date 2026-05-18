//
// Created by Jamal Abo Mokh on 17/05/2026.
//
#include "services/child_tracker.h"

#include <unistd.h>

#include "exceptions.h"

ChildTracker::ChildTracker() {
}

int ChildTracker::fork() {
    // call the os fork
    const int id = ::fork();

    if (id == -1)
        throw SyscallException("fork");

    if (id == 0) {
        // change group
        setpgrp();

        isChild = true;
    }

    return id;
}
