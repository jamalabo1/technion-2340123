//
// Created by jamal on 20/05/2026.
//
#include <unistd.h>

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"

using std::string;
using std::vector;
/*
 * UnSetEnvCommand
 */
REGISTER_COMMAND(UnSetEnvCommand);

void UnSetEnvCommand::execute() {
    auto pList = params()->list();
    if (pList.empty()) {
        throw CommandNotEnoughArguments();
    }

    std::string environ = "/proc/" + std::to_string(getpid()) + "/environ";

    FILE *env = fopen(environ.c_str(), "r");
    if (env == nullptr) {
        throw SyscallException("fopen");
    }

    string env_vars_raw;

    char buffer[512];

    // keep reading the environs
    while (true) {
        size_t bytes = fread(buffer, sizeof(char), sizeof(buffer), env);
        if (bytes > 0) {
            // add the received data
            env_vars_raw.append(buffer, bytes);
        }

        // if didn't get all requested data:
        if (bytes != sizeof(buffer)) {
            //close anyway
            fclose(env);
            // if was error
            if (ferror(env)) {
                throw SyscallException("fread");
            }
            // otherwise were done.
            break;
        }
    }

    // after reading the raw data, we use our beloved cpp to process
    std::vector<string> env_vars;
    int start = 0;
    while (start < env_vars_raw.size()) {
        int end = env_vars_raw.find('\0', start);
        if (end == std::string::npos) {
            // there is only one env var
            end = env_vars_raw.size();
        }
        env_vars.push_back(
            env_vars_raw.substr(start, end - start)
        );
        // skip this \0
        start = end + 1;
    }

    // we want an index lookup for the __environ
    std::map<string, int> env_var_map;

    for (int i = 0; i < env_vars.size(); ++i) {
        // we map each one to its index
        string key = Shell::Utils::_split(env_vars[i], "=")[0];
        env_var_map[key] = i;
    }

    std::vector<bool> delete_idx(env_vars.size(), false);

    // to save environ fixes.
    string throwException = "";

    for (const auto &env_var: pList) {
        auto it = env_var_map.find(env_var);

        if (it == env_var_map.end()) {
            //defer exception
            throwException = env_var;
            break;
        }

        delete_idx[it->second] = true;
    }

    int write = 0;

    for (int read = 0; read < env_vars.size(); ++read) {
        if (!delete_idx[read]) {
            __environ[write++] = __environ[read];
        }
    }

    // to make sure
    for (int i = write; i < env_vars.size(); ++i) {
        __environ[write] = nullptr;
    }
    if (!throwException.empty()) {
        throw CommandDomainError(throwException + " does not exist");
    }
}
