//
// Created by jamal on 27/05/2026.
//

#ifndef SCOPED_DUP2_H
#define SCOPED_DUP2_H

// same with fd but redirected
class ScopedDup2 {
private:
    int from;

    int to;

public:
    ScopedDup2(int from, int to);

    ~ScopedDup2();
};

#endif //SCOPED_DUP2_H
