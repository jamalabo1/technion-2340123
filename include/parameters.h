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
    std::string rawParams;
    std::vector<std::string> params;

public:
    Parameters(const std::string& raw_params, const std::vector<std::string>& params)
        : rawParams(raw_params),
          params(params)
    {
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

    std::string firstOrDefault() const
    {
        if (params.empty()) return "";

        return params[0];
    }

    std::vector<std::string> list() const
    {
        return params;
    }

    std::string concat() const
    {
        std::string concatenated;
        for (const auto& param : params)
        {
            concatenated += param;
        }
        return concatenated;
    }
};

#endif //PARAMETERS_H
