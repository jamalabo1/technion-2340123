//
// Created by Jamal Abo Mokh on 13/05/2026.
//
#include "context.h"

std::shared_ptr<WorkingEnvironment> CommandCtx::get_wenv() const {
    return wenv;
}

const Parameters& CommandCtx::get_params() const {
    return params;
}

std::string CommandCtx::get_cmdId() const {
    return commandId;
}

CommandCtx::CommandFlags CommandCtx::get_flags() const {
    return flags;
}

std::shared_ptr<Services> CommandCtx::get_services() const {
    return services;
}
