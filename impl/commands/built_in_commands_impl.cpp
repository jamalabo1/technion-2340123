//
// Created by Jamal Abo Mokh on 13/05/2026.
//

#include <iomanip>
#include <unistd.h>

#include "exceptions.h"
#include "utils.h"
#include "built_in.h"
#include <sys/utsname.h>

using std::string;

/*
 * ChangeTerminalNameCommand
 */
REGISTER_COMMAND(ChangeTerminalNameCommand);

void ChangeTerminalNameCommand::execute()
{
    // get the current working env
    auto wenv = ctx.get_wenv();

    auto termName = params()
        ->firstOrDefault();

    // if the params is empty
    if (termName.empty())
    {
        // reset to default if no arguments provided
        wenv->resetTerminalName();
    }
    else
    {
        // otherwise set to first param
        wenv->setTerminalName(termName);
    }
}

/*
 * JobsCommand
 */
REGISTER_COMMAND(JobsCommand);

void JobsCommand::execute()
{
    services->table->printJobsList();
}

/*
 * QuitCommand
 */
REGISTER_COMMAND(QuitCommand)

void QuitCommand::execute()
{
    bool hasKill = params()->firstOrDefault() == "kill";

    if (hasKill)
    {
        auto termName = ctx.get_wenv()->getTerminalName();

        // print current terminal name
        std::cout << termName << ": ";

        // hand it to the table for further printing and handling (although debatable if the table should be the one to do this)
        services->table->killAndRemoveAllJobs();
    }

    std::exit(0);
}

/*
 * ShowPidCommand
 */
REGISTER_COMMAND(ShowPidCommand);

void ShowPidCommand::execute()
{
    std::cout << "smash pid is " << getpid() << std::endl;
}

/*
 * GetCurrDirCommand
 */
REGISTER_COMMAND(GetCurrDirCommand);

void GetCurrDirCommand::execute()
{
    string currentDir = Shell::Utils::get_current_cwd();

    std::cout << currentDir << std::endl;
}

/*
 * ChangeDirCommand
 */
REGISTER_COMMAND(ChangeDirCommand);

void ChangeDirCommand::execute()
{
    auto path = params()->singleOrDefault<CommandTooManyArguments>();
    if (path.empty()) return;

    auto wenv = ctx.get_wenv();

    if (path == "-")
    {
        try
        {
            path = wenv->getLastWorkingDir();
        }
        catch (const std::exception&)
        {
            throw CommandErrorMessage("OLDPWD not set");
        }
    }

    // current state is [X]->[Y]

    if (chdir(path.c_str()) == -1)
    {
        throw SyscallException("chdir");
    }

    string resolvedPath = Shell::Utils::get_current_cwd();

    // we want the new one to be [Y]->[X]
    // so we need to push [Y]
    wenv->pushDir(resolvedPath);
}


/*
 * SysInfoCommand
 */
REGISTER_COMMAND(SysInfoCommand);

void SysInfoCommand::execute()
{
    string sysname = Shell::Utils::read_all("/proc/sys/kernel/ostype");
    string hostname = Shell::Utils::read_all("/proc/sys/kernel/hostname");
    string release = Shell::Utils::read_all("/proc/sys/kernel/osrelease");
    string stat = Shell::Utils::read_all("/proc/stat");

    std::regex pattern(R"((^|\n)btime\s+([0-9]+)(\n|$))");

    std::smatch match;
    if (!std::regex_search(stat, match, pattern))
    {
        throw std::logic_error("where is btime??");
    }

    long long btime = std::stoll(match[2].str());


    // stackoverflow says so...
    std::time_t boot_time = btime;
    struct tm tm_buf{};
    if (localtime_r(&boot_time, &tm_buf) == nullptr)
    {
        throw SyscallException("localtime_r");
    }


    //TODO: if not calling uname then where to get arch from? and besides even if they try to create a diff in the uname, /proc/ files
    // I don't think they will "fake" the arch right?

    // to find the arch (although also provides the other information readily)
    struct utsname buf{};
    //
    if (uname(&buf) == -1)
    {
        throw SyscallException("uname");
    };

    // since the read_all will also read the \n, so might as well just keep it (and don't add std::endl;)
    std::cout << "System: " << sysname;
    std::cout << "Hostname: " << hostname;
    std::cout << "Kernel: " << release;
    std::cout << "Architecture: " << buf.machine << std::endl;
    std::cout << "Boot Time: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << std::endl;
}
