//
// Created by Jamal Abo Mokh on 13/05/2026.
//
#include "wenv.h"

std::string WorkingEnvironment::getTerminalName() {
    return terminalName;
}

void WorkingEnvironment::setTerminalName(const std::string &tName) {
    terminalName = tName;
}

void WorkingEnvironment::resetTerminalName() {
    setTerminalName(appName);
}

std::string WorkingEnvironment::getAppName() const {
    return appName;
}

std::string WorkingEnvironment::getLastWorkingDir() const {
    return cds.previous();
}


std::string WorkingEnvironment::getCurrentWorkingDir() const {
    return cds.top();
}


void WorkingEnvironment::pushDir(const std::string &cd) {
    cds.push(cd);
}
