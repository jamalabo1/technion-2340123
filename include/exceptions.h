//
// Created by Jamal Abo Mokh on 15/05/2026.
//

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdexcept>
#include <string>

class CommandInvalidArguments : public std::invalid_argument
{
public:
    CommandInvalidArguments(): std::invalid_argument("invalid arguments")
    {
    }
};

class SyscallException : public std::runtime_error
{
public:
    SyscallException(std::string syscallName): std::runtime_error(syscallName)
    {
    }
};

#endif //EXCEPTIONS_H
