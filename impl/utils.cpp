//
// Created by jamal on 15/05/2026.
//

#include <string>
#include <vector>
#include <regex>

#include "utils.h"

#include <cstring>
#include <sstream>
#include <sys/wait.h>

#include "exceptions.h"

using namespace std;

namespace Shell
{
    namespace Utils
    {
        string _ltrim(const std::string& s)
        {
            size_t start = s.find_first_not_of(WHITESPACE);
            return (start == std::string::npos) ? "" : s.substr(start);
        }

        string _rtrim(const std::string& s)
        {
            size_t end = s.find_last_not_of(WHITESPACE);
            return (end == std::string::npos) ? "" : s.substr(0, end + 1);
        }

        string _trim(const std::string& s)
        {
            return _rtrim(_ltrim(s));
        }

        std::vector<std::string> _split(const std::string& param, const std::string& delim)
        {
            std::regex regexz(delim);
            std::vector<std::string> list(
                std::sregex_token_iterator(
                    param.begin(),
                    param.end(), regexz, -1
                ),
                std::sregex_token_iterator()
            );

            std::vector<string> result;
            // normalize and filter
            // TODO: must preserve order
            for (auto& basic_string : list)
            {
                basic_string = _trim(basic_string);
                if (!basic_string.empty())
                {
                    result.push_back(basic_string);
                }
            }


            return result;
        }

        int _parseCommandLine(const char* cmd_line, char** args)
        {
            FUNC_ENTRY()
            int i = 0;
            std::istringstream iss(_trim(string(cmd_line)).c_str());
            for (std::string s; iss >> s;)
            {
                args[i] = (char*)malloc(s.length() + 1);
                memset(args[i], 0, s.length() + 1);
                strcpy(args[i], s.c_str());
                args[++i] = NULL;
            }
            return i;
            FUNC_EXIT()
        }

        bool _isBackgroundComamnd(const char* cmd_line)
        {
            const string str(cmd_line);
            return str[str.find_last_not_of(WHITESPACE)] == '&';
        }

        void _removeBackgroundSign(char* cmd_line)
        {
            const string str(cmd_line);
            // find last character other than spaces
            unsigned int idx = str.find_last_not_of(WHITESPACE);
            // if all characters are spaces then return
            if (idx == string::npos)
            {
                return;
            }
            // if the command line does not end with & then return
            if (cmd_line[idx] != '&')
            {
                return;
            }
            // replace the & (background sign) with space and then remove all tailing spaces.
            cmd_line[idx] = ' ';
            // truncate the command line string up to the last non-space character
            cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
        }

        string _command(const string& cmd_line)
        {
            return cmd_line.substr(0, cmd_line.find_first_of(" \n"));
        }

        vector<string> _params(const string& trimmed)
        {
            size_t firstSpace = trimmed.find_first_of(" \t");
            string paramsRaw;
            if (firstSpace != string::npos)
            {
                // Substr from char after space to end
                paramsRaw = trimmed.substr(firstSpace + 1);
            }
            else
            {
                // No arguments, only command
                paramsRaw = "";
            }
            return _split(paramsRaw);
        }

        void KillProc(int pId)
        {
            SignalProc(pId, SIGKILL);
        }

        void SignalProc(int pId, int sigNum)
        {
            // send kill signal to pid
            int status = kill(pId, sigNum);
            if (status == -1)
            {
                throw SyscallException("kill");
            }
        }

        bool regex_contains(const std::string& str, const std::regex& regex)
        {
            return std::regex_match(str, regex);
        }

        short check_child(pid_t pid)
        {
            int status;


            auto result = waitpid(pid, &status, WNOHANG);


            if (result == 0)
            {
                // has not exited.
                return 1;
            }
            else if (result == pid)
            {
                // child exited
                return 0;
            }
            else
            {
                //TODO: check if actually error
                throw SyscallException("waitpid");
                // not relevant for tracking up to this point
                return -1;
            }
        }
    }
}
