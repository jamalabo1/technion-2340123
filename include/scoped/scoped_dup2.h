//
// Created by jamal on 27/05/2026.
//

#ifndef SCOPED_DUP2_H
#define SCOPED_DUP2_H
#include "scoped.h"

// same with fd but dup2
class ScopedDup2 : public Scoped {
private:
    int from;

    int to;

public:
    ScopedDup2(int from, int to);

    ~ScopedDup2();
};

#endif //SCOPED_DUP2_H
