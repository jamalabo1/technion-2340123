//
// Created by jamal on 27/05/2026.
//
#include <unistd.h>

#include "exceptions.h"
#include "scoped_dup2.h"

ScopedGuardedDup2::ScopedGuardedDup2(int from, int to) : Scoped(), guarded(to),
                                                         from(from),
                                                         to(to) {
    // this guarded to creates a temp copy of to, and now we replace it with from
    if (dup2(from, to) == -1) {
        throw SyscallException("dup2");
    }
    // when finishing guarded destructor will rever it back
}
