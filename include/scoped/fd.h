//
// Created by jamal on 27/05/2026.
//

#ifndef FD_H
#define FD_H
#include "scoped.h"

// so there is a problem with creating fds, that I have to close them when going out scope.
// and since I need a "finally" type thing (liek in c#) which is not in cpp
// and because of guarded syscalls (have expections in them), I need something to have automatic freeing, which is exactly scoped fd
class ScopedFd : public Scoped {
public:
    int fd;

    ScopedFd(int fd);
    ~ScopedFd();
};

#endif //FD_H
