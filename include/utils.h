//
// Created by jamal on 15/05/2026.
//

#ifndef UTILS_H
#define UTILS_H

#include "defines.h"
#include <regex>

namespace Shell
{
    namespace Utils
    {
        std::string _ltrim(const std::string& s);

        std::string _rtrim(const std::string& s);

        std::string _trim(const std::string& s);

        std::vector<std::string> _split(const std::string& param);

        int _parseCommandLine(const char* cmd_line, char** args);

        bool _isBackgroundComamnd(const char* cmd_line);

        void _removeBackgroundSign(char* cmd_line);

        bool regex_contains(const std::string& str, const std::regex& regex);

        std::string _command(const std::string& cmd_line);

        std::vector<std::string> _params(const std::string& trimmed);

        short check_child(pid_t pid);
    }
}


#endif //UTILS_H
