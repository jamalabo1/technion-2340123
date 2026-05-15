//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <string>

class Parameters {
protected:
    std::string rawParams;
    std::vector<std::string> params;

public:
    Parameters(const std::string &raw_params, const std::vector<std::string> &params)
        : rawParams(raw_params),
          params(params) {
    }

    template <class ErrorType>
    std::string single() {
        if (params.size() != 1) {
            throw ErrorType();
        }
        return params[0];
    }
};

#endif //PARAMETERS_H
