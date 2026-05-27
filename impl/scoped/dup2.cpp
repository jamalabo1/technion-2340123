//
// Created by jamal on 27/05/2026.
//
#include <unistd.h>

#include "exceptions.h"
#include "scoped_dup2.h"

ScopedDup2::ScopedDup2(int from, int to) : from(from), to(to) {
    if (dup2(from, to) == -1) {
        throw SyscallException("dup2");
    }
}

ScopedDup2::~ScopedDup2() {
    // when finishing restore
    if (dup2(to, from) == -1) {
        throw SyscallException("dup2");
    }
}
