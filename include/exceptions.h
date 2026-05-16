//
// Created by Jamal Abo Mokh on 15/05/2026.
//

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdexcept>
#include <string>

class CommandDomainError : public std::runtime_error
{
public:
    explicit CommandDomainError(const std::string& msg) : std::runtime_error(msg)
    {
    }
};


// general invalid arguments message
class CommandInvalidArguments : public CommandDomainError
{
public:
    CommandInvalidArguments(): CommandDomainError("invalid arguments")
    {
    }
};

// specific command error
class CommandErrorMessage : public CommandDomainError
{
public:
    explicit CommandErrorMessage(const std::string& msg): CommandDomainError(msg)
    {
    }
};

class CommandJobNotFoundException : public CommandErrorMessage
{
public:
    explicit CommandJobNotFoundException(const int& jobId) : CommandErrorMessage("job-id " + std::to_string(jobId) + " does not exist")
    {
    };
};


class SyscallException : public std::runtime_error
{
public:
    explicit SyscallException(const std::string& syscallName): std::runtime_error(syscallName)
    {
    }
};

#endif //EXCEPTIONS_H
