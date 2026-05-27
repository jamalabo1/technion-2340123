//
// Created by jamal on 27/05/2026.
//

#ifndef SCOPED_DUP_H
#define SCOPED_DUP_H
#include "scoped.h"

class ScopedDup : public Scoped {
private:
    int original_fd;
    int current_fd;

public:
    ScopedDup(int fd);

    ~ScopedDup();
};

#endif //SCOPED_DUP_H
