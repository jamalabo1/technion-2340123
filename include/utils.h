//
// Created by jamal on 15/05/2026.
//

#ifndef UTILS_H
#define UTILS_H

#include "defines.h"
#include <regex>

namespace Shell {
    namespace Utils {
        std::string _ltrim(const std::string &s);

        std::string _rtrim(const std::string &s);

        std::string _trim(const std::string &s);


        std::vector<std::string> _lsplit(const std::string &param, const std::string &delim);
        std::vector<std::string> _lpart(const std::string &param, const size_t &pos);

        std::vector<std::string> _split(const std::string &param, const std::string &delim = " ");

        int _parseCommandLine(const char *cmd_line, char **args);

        bool _checkAndRemoveBgCommand(std::string &cmd);

        bool regex_contains(const std::string &str, const std::regex &regex);

        bool contains_or(const std::string &str, std::vector<std::string> &patterns);

        std::string _command(const std::string &cmd_line);

        std::vector<std::string> _params(const std::string &trimmed);

        void KillProc(int pId);

        void SignalProc(int pId, int sigNum);

        short check_child(pid_t pid);

        std::string read_all(const std::string &path);

        std::string read_line(const std::string &path);

        // called from the above method after opening a file
        std::string read_all_f(FILE *file);

        std::string get_current_cwd();

        int parseInt(const std::string &s);

        // make sure to close fd.
        void traverse_dir(int fd, std::function<void(std::string)> cb);

        // first non pos;
        size_t _nonNpos(size_t a, size_t b);

        // command with pos
        std::pair<std::string, size_t> _commandWithPos(const std::string& cmd_line);

    }
}


#endif //UTILS_H
