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

        std::vector<std::string> _split(const std::string& param, const std::string& delim = " ");

        int _parseCommandLine(const char* cmd_line, char** args);

        bool _checkAndRemoveBgCommand(std::string &cmd);

        // bool _isBackgroundComamnd(const char* cmd_line);

        // void _removeBackgroundSign(std::string& cmd_line);

        bool regex_contains(const std::string& str, const std::regex& regex);
        bool contains_or(const std::string& str,  std::vector<std::string>& patterns);

        std::string _command(const std::string& cmd_line);

        std::vector<std::string> _params(const std::string& trimmed);

        void KillProc(int pId);

        void SignalProc(int pId, int sigNum);

        short check_child(pid_t pid);

        std::string read_all(const std::string& path);

        // called from the above method after opening a file
        std::string read_all_f(FILE* file);

    }
}


#endif //UTILS_H
