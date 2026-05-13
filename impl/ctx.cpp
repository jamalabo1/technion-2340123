//
// Created by Jamal Abo Mokh on 13/05/2026.
//
#include "context.h"

WorkingEnvironment & CommandCtx::get_wenv() const {
    return wenv;
}

Parameters * CommandCtx::get_params() const {
    return params;
}

