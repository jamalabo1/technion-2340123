//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#ifndef WENV_H
#define WENV_H

#include <string>

// working env, separated from the smash instance to enable it's sharing along other objects in the program if needed
class WorkingEnvironment
{
private:
    std::string terminalName;
    // default value
    std::string appName;

    std::string lastWorkingDir;

public:
    WorkingEnvironment(const std::string& appName) :
        terminalName(appName),
        appName(appName)
    {
    }

    std::string getTerminalName();
    void setTerminalName(const std::string& terminalName);
    // resets terminal name into the app name
    void resetTerminalName();

    std::string getAppName() const;
};


#endif //WENV_H
