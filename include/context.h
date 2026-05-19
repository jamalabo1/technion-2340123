//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef CONTEXT_H
#define CONTEXT_H

#include <utility>
#include <memory>
#include <string>

#include "parameters.h"
#include "services.h"
#include "wenv.h"


// command context (instead of drilling the params, and to allow future expansion)
class CommandCtx {
public:
    struct CommandFlags {
        bool isBackground;
    };

private:
    std::string commandId;

    // could be of different type based on the command.
    Parameters *params;

    // shared objs
    std::shared_ptr<WorkingEnvironment> wenv;

    // flags
    CommandFlags flags;

    // services
    std::shared_ptr<Services> services;

public:
    CommandCtx(
        std::string cmd_id,
        const std::shared_ptr<WorkingEnvironment> &wenv,
        Parameters *params,
        CommandFlags flags,
        const std::shared_ptr<Services> &services
    )
        : commandId(std::move(cmd_id)),
          params(params),
          wenv(wenv),
          flags(flags),
          services(services) {
    }

    std::shared_ptr<WorkingEnvironment> get_wenv() const;

    Parameters *get_params() const;

    std::string get_cmdName() const;

    std::string get_cmdId() const;

    CommandFlags get_flags() const;

    std::string get_cmdLine() const;

    std::shared_ptr<Services> get_services() const;
};

#endif //CONTEXT_H
