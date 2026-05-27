//
// Created by jamal on 27/05/2026.
//

#ifndef SCOPED_H
#define SCOPED_H

// general parent to prevent copy (double close)
class Scoped {
protected:
    Scoped() = default;

public:
    Scoped(const Scoped &) = delete;

    Scoped &operator=(const Scoped &) = delete;
};
#endif //SCOPED_H
