//
// Created by jamal on 27/05/2026.
//
#include <unistd.h>

#include "exceptions.h"
#include "scoped_dup.h"

ScopedDup::ScopedDup(int fd) : Scoped(), original_fd(fd) {
    current_fd = dup(fd);
    if (current_fd == -1) {
        throw SyscallException("dup");
    }
}

ScopedDup::~ScopedDup() {
    // then restore
    dup2(current_fd, original_fd);
    // make sure to close the duplicated one
    close(current_fd);
}
