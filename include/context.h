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
    std::string commandName;
    std::string commandId;

    std::string cmd_line;

    // could be of different type based on the command.
    Parameters* params;

    WorkingEnvironment& wenv;

public:
    CommandCtx(
        std::string cmd_line,
        std::string cmd_id,
        WorkingEnvironment &wenv,
        Parameters* params
        )
        : cmd_line(std::move(cmd_line)),
    commandId(cmd_id),
          params(params),
        wenv(wenv) {
    }

    WorkingEnvironment& get_wenv() const;
    Parameters* get_params() const;
    std::string get_cmdName() const;
    std::string get_cmdId() const;
};

#endif //CONTEXT_H
