#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "context.h"
#include "services.h"
#include "wenv.h"

namespace test_support {

inline std::string getCwd() {
    char* raw = getcwd(nullptr, 0);
    if (raw == nullptr) {
        throw std::runtime_error("getcwd failed");
    }

    std::string cwd(raw);
    free(raw);
    return cwd;
}

class ScopedCwd {
public:
    ScopedCwd() : original_(getCwd()) {
    }

    ~ScopedCwd() {
        chdir(original_.c_str());
    }

private:
    std::string original_;
};

class TempDir {
public:
    TempDir() {
        char path_template[] = "/tmp/smash_gtest_XXXXXX";
        char* created = mkdtemp(path_template);
        if (created == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        root_ = created;
    }

    ~TempDir() {
        for (auto it = files_.rbegin(); it != files_.rend(); ++it) {
            unlink(it->c_str());
        }
        for (auto it = dirs_.rbegin(); it != dirs_.rend(); ++it) {
            rmdir(it->c_str());
        }
        rmdir(root_.c_str());
    }

    const std::string& root() const {
        return root_;
    }

    std::string path(const std::string& name) const {
        return root_ + "/" + name;
    }

    std::string makeSubdir(const std::string& name) {
        const std::string dir = path(name);
        if (mkdir(dir.c_str(), 0700) == -1) {
            throw std::runtime_error("mkdir failed");
        }
        dirs_.push_back(dir);
        return dir;
    }

    std::string writeFile(const std::string& name, const std::string& contents) {
        const std::string file = path(name);
        std::ofstream out(file.c_str(), std::ios::binary);
        if (!out) {
            throw std::runtime_error("ofstream failed");
        }
        out << contents;
        out.close();
        files_.push_back(file);
        return file;
    }

    std::string makeExecutableScript(const std::string& name, const std::string& contents) {
        const std::string file = writeFile(name, contents);
        if (chmod(file.c_str(), 0700) == -1) {
            throw std::runtime_error("chmod failed");
        }
        return file;
    }

    std::string makeSymlink(const std::string& target, const std::string& name) {
        const std::string link = path(name);
        if (symlink(target.c_str(), link.c_str()) == -1) {
            throw std::runtime_error("symlink failed");
        }
        files_.push_back(link);
        return link;
    }

private:
    std::string root_;
    std::vector<std::string> files_;
    std::vector<std::string> dirs_;
};

inline std::string readFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("ifstream failed");
    }
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

inline std::shared_ptr<Services> makeServices() {
    return std::make_shared<Services>(
        std::make_shared<JobTable>(),
        std::make_shared<CommandProcManagement>(),
        std::make_shared<Aliases>(),
        std::make_shared<ChildTracker>()
    );
}

inline std::shared_ptr<WorkingEnvironment> makeWenv(const std::string& appName = "smash") {
    return std::make_shared<WorkingEnvironment>(appName);
}

inline CommandCtx makeCtx(
    const std::string& commandId,
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services,
    const std::vector<std::string>& args = {},
    bool isBackground = false
) {
    CommandCtx::CommandFlags flags;
    flags.isBackground = isBackground;

    return CommandCtx(
        commandId,
        wenv,
        Parameters(args),
        flags,
        services
    );
}

inline std::shared_ptr<CommandProc> makeProc(pid_t pid) {
    auto proc = std::make_shared<CommandProc>();
    proc->pId = pid;
    return proc;
}

class ScopedChild {
public:
    explicit ScopedChild(pid_t pid = -1) : pid_(pid), reaped_(pid <= 0) {
    }

    ScopedChild(const ScopedChild&) = delete;
    ScopedChild& operator=(const ScopedChild&) = delete;

    ScopedChild(ScopedChild&& other) noexcept
        : pid_(other.pid_),
          reaped_(other.reaped_) {
        other.pid_ = -1;
        other.reaped_ = true;
    }

    ScopedChild& operator=(ScopedChild&& other) noexcept {
        if (this != &other) {
            cleanup();
            pid_ = other.pid_;
            reaped_ = other.reaped_;
            other.pid_ = -1;
            other.reaped_ = true;
        }
        return *this;
    }

    ~ScopedChild() {
        cleanup();
    }

    pid_t pid() const {
        return pid_;
    }

    void markReaped() {
        reaped_ = true;
    }

    void cleanup() {
        if (reaped_ || pid_ <= 0) {
            return;
        }

        int status = 0;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        } else if (result == pid_) {
            reaped_ = true;
        }
    }

private:
    pid_t pid_;
    bool reaped_;
};

inline ScopedChild forkSleepingChild(unsigned seconds = 30) {
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        sleep(seconds);
        _exit(0);
    }
    return ScopedChild(pid);
}

inline ScopedChild forkExitingChild(int exitCode = 0) {
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        _exit(exitCode);
    }
    return ScopedChild(pid);
}

inline int waitFor(pid_t pid) {
    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        throw std::runtime_error(std::string("waitpid failed: ") + strerror(errno));
    }
    return status;
}

struct ChildRunResult {
    pid_t pid;
    int status;
    std::string stdoutOutput;
};

template <typename Func>
ChildRunResult runInChildCapturingStdout(Func func) {
    int pipeFds[2];
    if (pipe(pipeFds) == -1) {
        throw std::runtime_error("pipe failed");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        close(pipeFds[0]);
        if (dup2(pipeFds[1], STDOUT_FILENO) == -1) {
            _exit(10);
        }
        close(pipeFds[1]);

        try {
            func();
        } catch (...) {
            _exit(11);
        }
        _exit(0);
    }

    close(pipeFds[1]);
    std::string output;
    char buffer[512];
    while (true) {
        ssize_t bytes = read(pipeFds[0], buffer, sizeof(buffer));
        if (bytes > 0) {
            output.append(buffer, static_cast<std::size_t>(bytes));
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(pipeFds[0]);
        throw std::runtime_error("read failed");
    }
    close(pipeFds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        throw std::runtime_error("waitpid failed");
    }

    ChildRunResult result;
    result.pid = pid;
    result.status = status;
    result.stdoutOutput = output;
    return result;
}

} // namespace test_support

#endif // TEST_SUPPORT_H
