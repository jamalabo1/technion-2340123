//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <vector>
#include <string>

class Parameters
{
protected:
    std::vector<std::string> params;
    std::string raw;
    bool hasRaw;

    static std::string DEFAULT_VALUE;

public:
    explicit Parameters(const std::vector<std::string>& params, const std::string& = "");

    template <class ErrorType>
    std::string singleOrDefault(const std::string& defValue = DEFAULT_VALUE) const
    {
        if (params.empty()) return defValue;

        return single<ErrorType>();
    }

    template <class ErrorType>
    std::string single() const
    {
        if (params.size() != 1)
        {
            throw ErrorType();
        }
        return params[0];
    }


    // first or default value
    std::string firstOrDefault(const std::string& defValue = DEFAULT_VALUE) const;

    std::vector<std::string> list() const;

    std::string concat() const;

    std::string get_raw() const;

    // get raw has the cmd with in, so just takeout the command.
    std::string get_raw_param() const;
};

#endif //PARAMETERS_H
