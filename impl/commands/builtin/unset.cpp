//
// Created by jamal on 20/05/2026.
//
#include <unistd.h>
#include <unordered_map>

#include "exceptions.h"
#include "utils.h"
#include "commands/built_in.h"

using std::string;
using std::vector;
/*
 * UnSetEnvCommand
 */
REGISTER_COMMAND(UnSetEnvCommand);

void UnSetEnvCommand::execute()
{
    //TODO: do I have to make sync with /proc/ and __environ?
    auto pList = params()->list();
    if (pList.empty())
    {
        throw CommandNotEnoughArguments();
    }

    // environ path
    std::string environ = "/proc/" + std::to_string(getpid()) + "/environ";

    // read everything from the file
    string env_vars_raw = Shell::Utils::read_all(environ);

    // after reading the raw data, we use our beloved cpp to process
    std::vector<string> env_vars;
    size_t start = 0;
    while (start < env_vars_raw.size())
    {
        size_t end = env_vars_raw.find('\0', start);
        if (end == std::string::npos)
        {
            // there is only one env var
            end = env_vars_raw.size();
        }
        env_vars.push_back(
            env_vars_raw.substr(start, end - start)
        );
        // skip this \0
        start = end + 1;
    }

    // we want a O(1) lookup for the __environ
    std::unordered_map<string, bool> env_key_map;

    // now we have KEY=VALUE format in env_var, so we want to make it key "lookupable"
    for (const auto& env_var : env_vars)
    {
        // we map each one to its index

        // and the value can contain = according to man7, so we match the first =
        string key = env_var.substr(0, env_var.find_first_of("="));

        // first populate the table
        env_key_map[key] = false;
    }

    // to save environ fixes.
    string throwException;

    for (const auto& env_var : pList)
    {
        auto it = env_key_map.find(env_var);

        if (it == env_key_map.end())
        {
            //defer exception
            throwException = env_var;
            break;
        }
        // set for removal
        it->second = true;
    }

    int write = 0;
    int read = 0;

    while (__environ[read] != nullptr)
    {
        // if this env is not for deletion, then let's replace it with last index of deletion (write)
        // string key = Shell::Utils::_split(__environ[read], "=")[0];
        string var = __environ[read];

        string key = var.substr(0, var.find_first_of('='));

        if (
            // first match in our key maps (if not found then /proc/environ is stale?)
            env_key_map.find(key) == env_key_map.end()
            // or we know it exists, but we want to keep it (not remove it)
            || env_key_map[key] == false
        )
        {
            // since if we start with a,b,c not for deletion the indices write, read are equal
            __environ[write++] = __environ[read];
            // but if we encounter a,b,c, (d), e
            // such that d for deletion, then read increases but write is stuck at d, so next time, we perform __environ[d] = __environ[e]
            // and now, e is for available for replacement (since now write = e (old) )
        }
        read++;
    }

    // then remove at last write place

    // to make sure
    for (int i = write; i < read; ++i)
    {
        __environ[i] = nullptr;
    }

    // must be here to remove correctly found envs.
    if (!throwException.empty())
    {
        throw CommandDomainError(throwException + " does not exist");
    }
}
