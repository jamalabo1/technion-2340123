//
// Created by Jamal Abo Mokh on 13/05/2026.
//
#include "wenv.h"

std::string WorkingEnvironment::getTerminalName()
{
    return terminalName;
}

void WorkingEnvironment::setTerminalName(const std::string& tName)
{
    terminalName = tName;
}

void WorkingEnvironment::resetTerminalName()
{
    setTerminalName(appName);
}

std::string WorkingEnvironment::getAppName() const
{
    return appName;
}
