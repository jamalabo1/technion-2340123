//
// Created by jamal on 15/05/2026.
//

#include <string>
#include <vector>
#include <regex>

#include "utils.h"

#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>

#include <unistd.h>
#include <sys/syscall.h>

#include <dirent.h>
#include <unistd.h>

#include "exceptions.h"

using namespace std;

namespace Shell {
    namespace Utils {
        string _ltrim(const string &s) {
            size_t start = s.find_first_not_of(WHITESPACE);
            return (start == std::string::npos) ? "" : s.substr(start);
        }

        string _rtrim(const string &s) {
            size_t end = s.find_last_not_of(WHITESPACE);
            return (end == std::string::npos) ? "" : s.substr(0, end + 1);
        }

        string _trim(const string &s) {
            return _rtrim(_ltrim(s));
        }


        // left split, or first of split
        vector<string> _lsplit(const string &param, const string &delim) {
            size_t pos = param.find_first_of(delim);

            return _lpart(param, pos);
        }

        vector<string> _lpart(const string &param, const size_t &pos) {
            string elm1 = param.substr(0, pos);
            string elm2 = param.substr(pos + 1);

            return {elm1, elm2};
        }

        vector<string> _split(const string &param, const string &delim, bool normalize) {
            regex regexz(delim);
            vector<string> list(
                sregex_token_iterator(
                    param.begin(),
                    param.end(), regexz, -1
                ),
                sregex_token_iterator()
            );
            if (normalize) {
                vector<string> result;

                // normalize and filter
                for (auto &basic_string: list) {
                    basic_string = _trim(basic_string);
                    if (!basic_string.empty()) {
                        result.push_back(basic_string);
                    }
                }
                return result;
            } else {
                return list;
            }
        }

        // must be called after trim
        bool _checkAndRemoveBgCommand(string &cmd) {
            if (cmd.empty()) return false;

            if (cmd.back() != '&') {
                return false;
            }
            cmd.pop_back();

            cmd = _rtrim(cmd);

            return true;
        }

        string _command(const string &cmd_line) {
            // to allow for all whitespace types.
            return _commandWithPos(cmd_line).first;
        }


        bool _isWhitespaceChar(char c) {
            return
                    c == '\n' || c == '\r' || c == '\t' ||
                    c == '\f' || c == '\v' || c == ' ';
        }

        bool _isSplitChar(char c) {
            return c == '|' || c == '>' || c == '&' || _isWhitespaceChar(c);
        }

        pair<string, size_t> _commandWithPos(const string &cmd_line) {
            for (size_t i = 0; i < cmd_line.size(); i++) {
                char c = cmd_line[i];

                //\n\r\t\f\v
                // if special char then stop, it seems this is exactly std::isspace but keeping it for clarity
                if (_isSplitChar(c)) {
                    return {cmd_line.substr(0, i), i};
                }
            }
            // otherwise the whole string
            return {cmd_line, cmd_line.size()};
        }

        vector<string> _splitByCommandLogic(const string &cmd_line) {
            vector<string> parts;

            size_t start = 0;
            while (start < cmd_line.size()) {
                // skip separators
                while (start < cmd_line.size() && _isWhitespaceChar(cmd_line[start])) {
                    start++;
                }

                if (start == cmd_line.size()) {
                    break;
                }

                size_t end = start;
                while (end < cmd_line.size() && !_isWhitespaceChar(cmd_line[end])) {
                    end++;
                }

                parts.push_back(cmd_line.substr(start, end - start));
                start = end;
            }

            return parts;
        }

        vector<string> _params(const string &trimmed) {
            size_t firstSpace = trimmed.find_first_of(WHITESPACE);
            string paramsRaw;
            if (firstSpace != string::npos) {
                // Substr from char after space to end
                paramsRaw = trimmed.substr(firstSpace + 1);
            } else {
                // No arguments, only command
                paramsRaw = "";
            }

            return _splitByCommandLogic(paramsRaw);
        }

        void SignalProc(int pId, int sigNum) {
            // send kill signal to pid
            int status = kill(pId, sigNum);
            if (status == -1) {
                throw SyscallException("kill");
            }
        }

        void KillProc(int pId) {
            SignalProc(pId, SIGKILL);
        }


        bool contains_or(const string &str, vector<string> &patterns) {
            for (auto pattern: patterns) {
                if (str.find_first_of(pattern) != string::npos) {
                    return true;
                }
            }
            return false;
        }

        short check_child(pid_t pid) {
            int status;


            auto result = waitpid(pid, &status, WNOHANG);


            if (result == 0) {
                // has not exited.
                return 1;
            } else if (result == pid) {
                // child exited
                return 0;
            } else {
                if (errno == EINTR) {
                    // if was interrupted
                }
                //TODO: check if actually error
                throw SyscallException("waitpid");
                // not relevant for tracking up to this point
                return -1;
            }
        }

        string read_all(const string &path) {
            FILE *fd = fopen(path.c_str(), "r");

            if (fd == nullptr) {
                throw SyscallException("open");
            }

            // read_all_f closes the file
            return read_all_f(fd);
        }

        string read_all_f(FILE *fd) {
            char buf[512];

            string data;

            static constexpr int SIZE = sizeof(buf);

            while (true) {
                size_t bytes = fread(buf, sizeof(char), SIZE, fd);
                // read failed
                if (ferror(fd)) {
                    //close and report
                    fclose(fd);
                    throw SyscallException("read");
                }
                // if we have any data, then add it
                if (bytes > 0) {
                    // add the received data
                    data += string(buf, bytes);
                }

                // we didnt get data as much as we requested, so there will be no further data to be read.
                if (bytes < SIZE) {
                    fclose(fd);
                    break; // EOF
                }
            }

            return data;
        }

        std::string read_line(const std::string &path) {
            string data = read_all(path);
            // read until first newline
            return data.substr(0, data.find_first_of('\n'));
        }

        string get_current_cwd() {
            // according to man7, nullptr, 0 in posix glibc ext, dynamically allocates the buffer's size.
            // but it's our responsibility to free it
            char *buf = getcwd(nullptr, 0);
            if (buf == nullptr) {
                throw SyscallException("getcwd");
            }

            // copy the results (it's null terminated)
            string result(buf);

            // then free it
            free(buf);

            return result;
        }

        int parseInt(const std::string &s) {
            size_t pos;
            int result = std::stoi(s, &pos);
            if (pos != s.length()) {
                // maybe 1x? (also 01 is not accepted under this test, maybe change the test?)
                throw std::invalid_argument("Not Int");
            }
            return result;
        }

        struct linux_dirent64 {
            ino64_t d_ino; /* 64-bit inode number */
            off64_t d_off; /* Not an offset; see getdents() */
            unsigned short d_reclen; /* Size of this dirent */
            unsigned char d_type; /* File type */
            char d_name[]; /* Filename (null-terminated) */
        };

        // returns true if there is more to read, must close fd
        void traverse_dir(int fd, std::function<void(string)> cb) {
            constexpr size_t BUF_SIZE = 8192;
            char buf[BUF_SIZE];

            while (true) {
                // in glib2.27,getdents64 is not defined, so a direct syscall
                const long dents = syscall(SYS_getdents64, fd, buf, BUF_SIZE);
                if (dents == -1) {
                    // close and report
                    close(fd);
                    throw SyscallException("getdents64");
                }

                if (dents == 0)
                    // we finished this directory
                    break;

                for (size_t bpos = 0; bpos < dents;) {
                    auto *d = reinterpret_cast<linux_dirent64 *>(buf + bpos);

                    cb(d->d_name);

                    bpos += d->d_reclen;
                }
            }
        }
    }
}
