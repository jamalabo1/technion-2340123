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

    static std::string DEFAULT_VALUE;

public:
    explicit Parameters(const std::vector<std::string>& params);

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
};

#endif //PARAMETERS_H
