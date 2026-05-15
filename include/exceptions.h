//
// Created by Jamal Abo Mokh on 15/05/2026.
//

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdexcept>

class CommandInvalidArguments : public std::invalid_argument {
public:
    CommandInvalidArguments(): std::invalid_argument("invalid arguments") {}
};

#endif //EXCEPTIONS_H
