//
// Created by jamal on 27/05/2026.
//

#ifndef SCOPED_DUP2_H
#define SCOPED_DUP2_H
#include "scoped.h"
#include "scoped_dup.h"

// same with fd but dup2
class ScopedGuardedDup2 : public Scoped {
private:
    ScopedDup guarded;

    int from;
    int to;

public:
    ScopedGuardedDup2(int from, int to);
};

#endif //SCOPED_DUP2_H
