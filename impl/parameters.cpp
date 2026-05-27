//
// Created by jamal on 16/05/2026.
//
#include "parameters.h"

#include "utils.h"

using namespace std;

// init static default
string Parameters::DEFAULT_VALUE;


Parameters::Parameters(
    const vector<string>& params,
    const string& raw
    )
    : params(params),raw(raw), hasRaw(!raw.empty())
{
}

string Parameters::firstOrDefault(const string& defValue) const
{
    if (params.empty()) return defValue;

    return params[0];
}

vector<string> Parameters::list() const
{
    return params;
}

string Parameters::concat() const

{
    std::string concatenated;

    size_t n = params.size();
    for (size_t i = 0; i < n; i++)
    {
        concatenated += params[i];
        if (i != n - 1)
        {
            // reconstruct a valid whitespace
            concatenated += " ";
        }
    }

    return concatenated;
}

std::string Parameters::get_raw() const {
    return raw;
}

std::string Parameters::get_raw_param() const {
    auto p =Shell::Utils::_commandWithPos(get_raw());

    // this is where I think the command is.
    size_t pos =  p.second;
    //so what after pos, is the thing i want to take out

    return raw.substr(pos);
}


