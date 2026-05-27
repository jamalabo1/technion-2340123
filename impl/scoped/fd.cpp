//
// Created by jamal on 27/05/2026.
//
#include "fd.h"

#include <unistd.h>

#include "exceptions.h"

ScopedFd::ScopedFd(int fd) : Scoped(), fd(fd) {
}

ScopedFd::~ScopedFd() {
    // close
    close(fd);
}
