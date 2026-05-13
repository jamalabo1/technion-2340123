//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef CONTEXT_H
#define CONTEXT_H

#include <utility>
#include <vector>
#include <string>

#include "parameters.h"
#include "wenv.h"

// command context (instead of drilling the params, and to allow future expansion)
class CommandCtx {
private:
    std::string cmd_line;

    // could be of different type based on the command.
    Parameters* params;

    WorkingEnvironment& wenv;

public:
    CommandCtx(
        std::string cmd_line,
        WorkingEnvironment &wenv,
        Parameters* params
        )
        : cmd_line(std::move(cmd_line)),
          wenv(wenv),
    params(params) {
    }

    WorkingEnvironment& get_wenv() const;
    Parameters* get_params() const;
};

#endif //CONTEXT_H
