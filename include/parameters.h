//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <string>

// abstract parameters
class Parameters {
protected:
    std::string rawParams;

public:
    template <class ErrorType>
    std::string single() {
        if (more) {
        throw ErrorType();
        }
    }
};

#endif //PARAMETERS_H
