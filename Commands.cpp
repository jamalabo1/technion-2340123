#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"

#include <regex>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

std::vector<std::string> _split(const std::string& param) {
    std::regex regexz(" ");
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
    for (auto& basic_string : list) {
        basic_string = _trim(basic_string);
        if (!basic_string.empty()) {
            result.push_back(basic_string);
        }
    }


    return result;
}

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

bool _isBackgroundComamnd(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

// TODO: Add your implementation for classes in Commands.h 

SmallShell::SmallShell() {
    // TODO: add your implementation
}

SmallShell::~SmallShell() {
    // TODO: add your implementation
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line) {
    // For example:
    /*
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

    if (firstWord.compare("pwd") == 0) {
      return new GetCurrDirCommand(cmd_line);
    }
    else if (firstWord.compare("showpid") == 0) {
      return new ShowPidCommand(cmd_line);
    }
    else if ...
    .....
    else {
      return new ExternalCommand(cmd_line);
    }
    */
    string cmd_s = _trim(string(cmd_line));
    string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));

//     string paramsRaw = cmd_s.substr(
// //TODO: change to end of the first occur instead (can fail on chprompt chprompt!!)
//     cmd_s.find_last_of(firstWord) + 1
//     );

    size_t firstSpace = cmd_s.find_first_of(" \t");
    string paramsRaw;
    if (firstSpace != string::npos) {
        // Substr from char after space to end
        paramsRaw = cmd_s.substr(firstSpace + 1);
    } else {
        // No arguments, only command
        paramsRaw = "";
    }

    std::vector<std::string> paramsList = _split(paramsRaw);

    Parameters *params = new Parameters(
paramsRaw,
paramsList
    );

    CommandCtx ctx(
        cmd_s,
        firstWord,
        wenv,
        params
    );

    if (firstWord.compare("chprompt") == 0) {
        return new ChangeTerminalNameCommand(ctx);
    }

    return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
    // TODO: Add your implementation here
    // for example:
    Command *cmd = CreateCommand(cmd_line);

    try {
        cmd->execute();
    } catch (const std::exception &e) {
        CommandCtx& ctx = cmd->get_ctx();

        string terminalName = ctx.get_wenv().getTerminalName();

        std::cerr << terminalName << " error: " <<  ctx.get_cmdId() << ":" << e.what() << std::endl;
    }

    // Please note that you must fork smash process for some commands (e.g., external commands....)
}
