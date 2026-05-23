#include <gtest/gtest.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits.h>
#include <map>
#include <pwd.h>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../test_support.h"

extern char** environ;

namespace {

const char* kPrompt = "smash> ";

std::string dirnameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string sourceRoot() {
    const std::string file = __FILE__;
    const std::string suffix = "/tests/integration/smash_process_integration_test.cpp";
    const std::size_t pos = file.rfind(suffix);
    if (pos != std::string::npos) {
        return file.substr(0, pos);
    }
    return ".";
}

std::string smashBinary() {
    const char* explicitPath = getenv("SMASH_TEST_BINARY");
    if (explicitPath != nullptr && explicitPath[0] != '\0') {
        return explicitPath;
    }

    char exePath[PATH_MAX];
    const ssize_t bytes = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (bytes > 0) {
        exePath[bytes] = '\0';
        const std::string sibling = dirnameOf(exePath) + "/skeleton_smash";
        if (access(sibling.c_str(), X_OK) == 0) {
            return sibling;
        }
    }

    return "./skeleton_smash";
}

bool fileExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

std::string readTextFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path);
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string joinPath(const std::string& base, const std::string& child) {
    if (base.empty() || base[base.size() - 1] == '/') {
        return base + child;
    }
    return base + "/" + child;
}

void collectFilesRecursive(const std::string& root, std::vector<std::string>& files) {
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return;
    }

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string path = joinPath(root, name);
        struct stat st {};
        if (lstat(path.c_str(), &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            collectFilesRecursive(path, files);
        } else if (path.size() >= 2 &&
                   (path.substr(path.size() - 2) == ".h" ||
                    (path.size() >= 4 && path.substr(path.size() - 4) == ".cpp"))) {
            files.push_back(path);
        }
    }

    closedir(dir);
}

std::vector<std::string> productionFiles() {
    const std::string root = sourceRoot();
    std::vector<std::string> files;

    const char* topLevel[] = {
        "Commands.cpp",
        "Commands.h",
        "signals.cpp",
        "signals.h",
        "smash.cpp",
        nullptr
    };
    for (const char** path = topLevel; *path != nullptr; ++path) {
        const std::string full = joinPath(root, *path);
        if (fileExists(full)) {
            files.push_back(full);
        }
    }

    collectFilesRecursive(joinPath(root, "impl"), files);
    collectFilesRecursive(joinPath(root, "include"), files);
    std::sort(files.begin(), files.end());
    return files;
}

std::string relativePath(const std::string& path) {
    const std::string root = sourceRoot();
    if (path.find(root) == 0) {
        std::string rel = path.substr(root.size());
        if (!rel.empty() && rel[0] == '/') {
            rel.erase(rel.begin());
        }
        return rel;
    }
    return path;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string stripPrompts(std::string text, const std::vector<std::string>& promptNames = {"smash"}) {
    for (const std::string& prompt : promptNames) {
        replaceAll(text, prompt + "> ", "");
    }
    return text;
}

std::vector<std::string> outputLines(const std::string& text,
                                     const std::vector<std::string>& promptNames = {"smash"}) {
    std::vector<std::string> lines;
    std::istringstream input(stripPrompts(text, promptNames));
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string joinCommands(const std::vector<std::string>& commands) {
    std::string input;
    for (const std::string& command : commands) {
        input += command;
        input += "\n";
    }
    return input;
}

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl F_SETFL failed");
    }
}

pid_t parseProcParent(pid_t pid) {
    std::ifstream statFile(("/proc/" + std::to_string(pid) + "/stat").c_str());
    std::string statLine;
    std::getline(statFile, statLine);
    const std::size_t endComm = statLine.rfind(") ");
    if (endComm == std::string::npos) {
        return -1;
    }

    std::istringstream rest(statLine.substr(endComm + 2));
    char state = '\0';
    pid_t ppid = -1;
    rest >> state >> ppid;
    return ppid;
}

std::vector<pid_t> descendantsOf(pid_t rootPid) {
    std::map<pid_t, std::vector<pid_t>> childrenByParent;
    DIR* proc = opendir("/proc");
    if (proc == nullptr) {
        return {};
    }

    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        long pidLong = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || pidLong <= 0) {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pidLong);
        const pid_t ppid = parseProcParent(pid);
        if (ppid > 0) {
            childrenByParent[ppid].push_back(pid);
        }
    }

    closedir(proc);

    std::vector<pid_t> result;
    std::vector<pid_t> stack = childrenByParent[rootPid];
    while (!stack.empty()) {
        const pid_t pid = stack.back();
        stack.pop_back();
        result.push_back(pid);
        const auto it = childrenByParent.find(pid);
        if (it != childrenByParent.end()) {
            stack.insert(stack.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

void killPid(pid_t pid, int signalNumber = SIGKILL) {
    if (pid != 0) {
        kill(pid, signalNumber);
    }
}

std::string procCmdline(pid_t pid) {
    std::ifstream in(("/proc/" + std::to_string(pid) + "/cmdline").c_str(), std::ios::binary);
    if (!in) {
        return "";
    }
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::replace(data.begin(), data.end(), '\0', ' ');
    return data;
}

void killByNeedles(const std::vector<std::string>& needles) {
    if (needles.empty()) {
        return;
    }

    DIR* proc = opendir("/proc");
    if (proc == nullptr) {
        return;
    }

    while (dirent* entry = readdir(proc)) {
        char* end = nullptr;
        long pidLong = strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || pidLong <= 0) {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(pidLong);
        const std::string cmdline = procCmdline(pid);
        for (const std::string& needle : needles) {
            if (!needle.empty() && contains(cmdline, needle)) {
                killPid(pid);
                break;
            }
        }
    }

    closedir(proc);
}

int fdCount(pid_t pid) {
    const std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR* dir = opendir(fdDir.c_str());
    if (dir == nullptr) {
        return -1;
    }

    int count = 0;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

long expectedDuBlocks(const std::string& path) {
    struct stat st {};
    if (lstat(path.c_str(), &st) == -1) {
        throw std::runtime_error("lstat failed");
    }

    long blocks = st.st_blocks;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr) {
            throw std::runtime_error("opendir failed");
        }
        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            blocks += expectedDuBlocks(joinPath(path, name));
        }
        closedir(dir);
    }
    return blocks;
}

long expectedDuKb(const std::string& path) {
    const long blocks = expectedDuBlocks(path);
    return (blocks + 1) / 2;
}

std::vector<std::string> makeEnv(const std::map<std::string, std::string>& overrides) {
    std::vector<std::string> env;
    std::set<std::string> overrideKeys;
    for (const auto& kv : overrides) {
        overrideKeys.insert(kv.first);
    }

    for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
        const std::string entry = *current;
        const std::size_t eq = entry.find('=');
        const std::string key = eq == std::string::npos ? entry : entry.substr(0, eq);
        if (overrideKeys.find(key) == overrideKeys.end()) {
            env.push_back(entry);
        }
    }

    for (const auto& kv : overrides) {
        env.push_back(kv.first + "=" + kv.second);
    }
    return env;
}

struct SpawnedProcess {
    pid_t pid = -1;
    int stdinFd = -1;
    int stdoutFd = -1;
    int stderrFd = -1;
};

SpawnedProcess spawnSmash(const std::string& cwd, const std::map<std::string, std::string>& envOverrides) {
    int stdinPipe[2];
    int stdoutPipe[2];
    int stderrPipe[2];
    if (pipe(stdinPipe) == -1 || pipe(stdoutPipe) == -1 || pipe(stderrPipe) == -1) {
        throw std::runtime_error("pipe failed");
    }

    const std::vector<std::string> envStorage = makeEnv(envOverrides);
    const std::string binary = smashBinary();

    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        setpgrp();
        if (!cwd.empty() && chdir(cwd.c_str()) == -1) {
            _exit(111);
        }

        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);

        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(binary.c_str()));
        argv.push_back(nullptr);

        if (envOverrides.empty()) {
            execv(binary.c_str(), argv.data());
        } else {
            std::vector<char*> envp;
            for (const std::string& entry : envStorage) {
                envp.push_back(const_cast<char*>(entry.c_str()));
            }
            envp.push_back(nullptr);
            execve(binary.c_str(), argv.data(), envp.data());
        }
        _exit(112);
    }

    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    setNonBlocking(stdoutPipe[0]);
    setNonBlocking(stderrPipe[0]);

    SpawnedProcess process;
    process.pid = pid;
    process.stdinFd = stdinPipe[1];
    process.stdoutFd = stdoutPipe[0];
    process.stderrFd = stderrPipe[0];
    return process;
}

struct SmashRunResult {
    int status = -1;
    bool timedOut = false;
    long elapsedMs = 0;
    std::string stdoutOutput;
    std::string stderrOutput;

    int exitCode() const {
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
};

class SmashSession {
public:
    SmashSession(const std::string& cwd,
                 const std::map<std::string, std::string>& envOverrides = {},
                 const std::vector<std::string>& cleanupNeedles = {})
        : process_(spawnSmash(cwd, envOverrides)),
          cleanupNeedles_(cleanupNeedles),
          start_(std::chrono::steady_clock::now()) {
    }

    ~SmashSession() {
        if (!finished_) {
            cleanup();
        }
        closeFd(process_.stdinFd);
        closeFd(process_.stdoutFd);
        closeFd(process_.stderrFd);
    }

    pid_t pid() const {
        return process_.pid;
    }

    void writeInput(const std::string& input) {
        const char* data = input.data();
        std::size_t remaining = input.size();
        while (remaining > 0) {
            ssize_t written = write(process_.stdinFd, data, remaining);
            if (written > 0) {
                data += written;
                remaining -= static_cast<std::size_t>(written);
                continue;
            }
            if (written == -1 && errno == EINTR) {
                continue;
            }
            if (written == -1 && errno == EPIPE) {
                return;
            }
            throw std::runtime_error("write to smash failed");
        }
        pumpFor(20);
    }

    void sendSignal(int signalNumber) {
        killPid(process_.pid, signalNumber);
        pumpFor(20);
    }

    void sleepAndDrain(int milliseconds) {
        pumpFor(milliseconds);
    }

    SmashRunResult finish(int timeoutMs) {
        closeFd(process_.stdinFd);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while ((!childDone_ || process_.stdoutFd != -1 || process_.stderrFd != -1) &&
               std::chrono::steady_clock::now() < deadline) {
            pumpFor(25);
        }

        SmashRunResult result;
        if (!childDone_ || process_.stdoutFd != -1 || process_.stderrFd != -1) {
            result.timedOut = true;
            cleanup();
        } else {
            result.status = status_;
        }

        pumpFor(25);
        result.stdoutOutput = stdout_;
        result.stderrOutput = stderr_;
        result.elapsedMs = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_
            ).count()
        );
        finished_ = true;
        return result;
    }

private:
    static void closeFd(int& fd) {
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    }

    void cleanup() {
        for (pid_t child : descendantsOf(process_.pid)) {
            killPid(child);
        }
        killPid(-process_.pid);
        killPid(process_.pid);
        killByNeedles(cleanupNeedles_);
        int status = 0;
        waitpid(process_.pid, &status, 0);
        childDone_ = true;
        status_ = status;
    }

    void readAvailable(int& fd, std::string& output) {
        if (fd == -1) {
            return;
        }

        char buffer[1024];
        while (true) {
            ssize_t bytes = read(fd, buffer, sizeof(buffer));
            if (bytes > 0) {
                output.append(buffer, static_cast<std::size_t>(bytes));
                continue;
            }
            if (bytes == 0) {
                closeFd(fd);
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw std::runtime_error("read from smash failed");
        }
    }

    void reapIfDone() {
        if (childDone_) {
            return;
        }

        int status = 0;
        pid_t result = waitpid(process_.pid, &status, WNOHANG);
        if (result == process_.pid) {
            childDone_ = true;
            status_ = status;
        }
    }

    void pumpFor(int milliseconds) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            pollfd fds[2];
            nfds_t count = 0;
            if (process_.stdoutFd != -1) {
                fds[count].fd = process_.stdoutFd;
                fds[count].events = POLLIN | POLLHUP | POLLERR;
                ++count;
            }
            if (process_.stderrFd != -1) {
                fds[count].fd = process_.stderrFd;
                fds[count].events = POLLIN | POLLHUP | POLLERR;
                ++count;
            }

            int timeout = 5;
            if (count > 0) {
                poll(fds, count, timeout);
            } else {
                usleep(static_cast<useconds_t>(timeout * 1000));
            }

            readAvailable(process_.stdoutFd, stdout_);
            readAvailable(process_.stderrFd, stderr_);
            reapIfDone();
        } while (std::chrono::steady_clock::now() < deadline);
    }

    SpawnedProcess process_;
    std::vector<std::string> cleanupNeedles_;
    std::chrono::steady_clock::time_point start_;
    bool finished_ = false;
    bool childDone_ = false;
    int status_ = -1;
    std::string stdout_;
    std::string stderr_;
};

SmashRunResult runSmash(const std::vector<std::string>& commands,
                        const std::string& cwd = sourceRoot(),
                        int timeoutMs = 5000,
                        const std::map<std::string, std::string>& envOverrides = {},
                        const std::vector<std::string>& cleanupNeedles = {}) {
    SmashSession session(cwd, envOverrides, cleanupNeedles);
    session.writeInput(joinCommands(commands));
    return session.finish(timeoutMs);
}

void expectExitedZero(const SmashRunResult& result) {
    ASSERT_FALSE(result.timedOut) << "smash timed out\nstdout:\n" << result.stdoutOutput
                                  << "\nstderr:\n" << result.stderrOutput;
    ASSERT_TRUE(WIFEXITED(result.status)) << "smash did not exit normally";
    EXPECT_EQ(0, WEXITSTATUS(result.status));
}

bool regexSearch(const std::string& text, const std::string& pattern) {
    return std::regex_search(text, std::regex(pattern));
}

} // namespace

TEST(StaticSourceContractTest, MakefileBuildsTheRealImplementation) {
    const std::string makefile = readTextFile(joinPath(sourceRoot(), "Makefile"));

    EXPECT_TRUE(contains(makefile, "make smash") || contains(makefile, "$(SMASH_BIN)"));
    EXPECT_TRUE(contains(makefile, "impl") || contains(makefile, "include"))
        << "Makefile should compile the implementation files used by CMake";
}

TEST(StaticSourceContractTest, ProductionDoesNotUseForbiddenHighLevelApis) {
    const std::vector<std::pair<std::string, std::regex>> forbidden = {
        {"fstream", std::regex("\\bfstream\\b")},
        {"ofstream", std::regex("\\bofstream\\b")},
        {"ifstream", std::regex("\\bifstream\\b")},
        {"opendir", std::regex("\\bopendir\\s*\\(")},
        {"readdir", std::regex("\\breaddir\\s*\\(")},
        {"closedir", std::regex("\\bclosedir\\s*\\(")},
        {"system", std::regex("\\bsystem\\s*\\(")},
        {"popen", std::regex("\\bpopen\\s*\\(")}
    };

    std::vector<std::string> violations;
    for (const std::string& path : productionFiles()) {
        const std::string text = readTextFile(path);
        for (const auto& check : forbidden) {
            if (std::regex_search(text, check.second)) {
                violations.push_back(relativePath(path) + " uses " + check.first);
            }
        }
    }

    EXPECT_TRUE(violations.empty()) << "Forbidden API violations:\n"
                                    << stripPrompts(joinCommands(violations));
}

TEST(StaticSourceContractTest, WhoamiDoesNotUseGetpwuid) {
    std::vector<std::string> offenders;
    for (const std::string& path : productionFiles()) {
        const std::string text = readTextFile(path);
        if (std::regex_search(text, std::regex("\\bgetpwuid\\s*\\("))) {
            offenders.push_back(relativePath(path));
        }
    }
    EXPECT_TRUE(offenders.empty()) << "HW FAQ forbids getpwuid() for whoami:\n"
                                   << stripPrompts(joinCommands(offenders));
}

TEST(StaticSourceContractTest, ComplexExternalCommandUsesBinBashPath) {
    const std::string path = joinPath(sourceRoot(), "impl/commands/external_commands.cpp");
    ASSERT_TRUE(fileExists(path));
    const std::string text = readTextFile(path);

    EXPECT_TRUE(contains(text, "/bin/bash"))
        << "Complex external commands must exec /bin/bash explicitly";
    EXPECT_FALSE(contains(text, "execvp(\"bash\""))
        << "PATH lookup for bash is weaker than the spec's /bin/bash requirement";
}

TEST(SmashProcessIntegrationTest, QuitExitsCleanly) {
    const SmashRunResult result = runSmash({"quit"});

    expectExitedZero(result);
    EXPECT_EQ(kPrompt, result.stdoutOutput);
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, EmptyInputIsIgnored) {
    SmashSession session(sourceRoot());
    session.writeInput("\nquit\n");
    const SmashRunResult result = session.finish(5000);

    expectExitedZero(result);
    EXPECT_EQ(std::string(kPrompt) + kPrompt, result.stdoutOutput);
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, ChpromptShowpidAndPwdIgnoreExtraArguments) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "chprompt custom ignored",
        "showpid ignored",
        "pwd ignored",
        "chprompt",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(
        result.stdoutOutput,
        "^smash> custom> smash pid is [0-9]+\ncustom> " + temp.root() + "\ncustom> smash> $"
    )) << result.stdoutOutput;
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, CdEdgeCasesAndErrorStream) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "pwd",
        "cd",
        "pwd",
        "cd -",
        "cd missing",
        "cd a b",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{temp.root(), temp.root()}), outputLines(result.stdoutOutput));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: cd: OLDPWD not set\n"));
    EXPECT_TRUE(regexSearch(result.stderrOutput, "smash error: chdir failed: .+\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: cd: too many arguments\n"));
}

TEST(SmashProcessIntegrationTest, CdDashTogglesBetweenLastTwoDirectories) {
    test_support::TempDir temp;
    const std::string sub = temp.makeSubdir("sub");

    const SmashRunResult result = runSmash({
        "pwd",
        "cd sub",
        "pwd",
        "cd -",
        "pwd",
        "cd -",
        "pwd",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{temp.root(), sub, temp.root(), sub}),
              outputLines(result.stdoutOutput));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, JobsListsSortedAndQuitKillReportsAllJobs) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 5&",
        "/bin/sleep 5&",
        "jobs",
        "quit kill"
    }, sourceRoot(), 2000);

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[1\\] /bin/sleep 5&\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[2\\] /bin/sleep 5&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 2 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, JobsRemovesFinishedJobsBeforePrinting) {
    const SmashRunResult result = runSmash({
        "/bin/true&",
        "/bin/sleep 0.1",
        "jobs",
        "quit kill"
    }, sourceRoot(), 3000);

    expectExitedZero(result);
    EXPECT_FALSE(contains(result.stdoutOutput, "[1] /bin/true&"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
}

TEST(SmashProcessIntegrationTest, FgErrorsAreExact) {
    const SmashRunResult result = runSmash({
        "fg",
        "fg abc",
        "fg 1x",
        "fg 99",
        "fg 1 2",
        "quit"
    });

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: fg: jobs list is empty\n"));
    EXPECT_EQ(3, countOccurrences(result.stderrOutput, "smash error: fg: invalid arguments\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: fg: job-id 99 does not exist\n"));
}

TEST(SmashProcessIntegrationTest, FgWithoutIdUsesMaxJobIdAndRemovesIt) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 1.5&",
        "/bin/sleep 1&",
        "fg",
        "quit kill"
    }, sourceRoot(), 3000);

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "/bin/sleep 1& [0-9]+\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, KillSendsSignalAndJobIsRemovedAfterReap) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 5&",
        "kill -9 1",
        "/bin/sleep 0.1",
        "jobs",
        "quit kill"
    }, sourceRoot(), 3000);

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 9 was sent to pid [0-9]+\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, KillErrorsAreExact) {
    const SmashRunResult result = runSmash({
        "kill 9 1",
        "kill -x 1",
        "kill -9 1x",
        "kill -9 1",
        "quit"
    });

    expectExitedZero(result);
    EXPECT_EQ(3, countOccurrences(result.stderrOutput, "smash error: kill: invalid arguments\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: kill: job-id 1 does not exist\n"));
}

TEST(SmashProcessIntegrationTest, QuitKillPrintsZeroJobs) {
    const SmashRunResult result = runSmash({"quit kill"});

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
}

TEST(SmashProcessIntegrationTest, AliasListingOrderAndExpansion) {
    const SmashRunResult result = runSmash({
        "alias z='echo last'",
        "alias a='echo first'",
        "alias",
        "z",
        "a",
        "quit"
    });

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    const auto z = std::find(lines.begin(), lines.end(), "z='echo last'");
    const auto a = std::find(lines.begin(), lines.end(), "a='echo first'");
    ASSERT_NE(lines.end(), z);
    ASSERT_NE(lines.end(), a);
    EXPECT_LT(std::distance(lines.begin(), z), std::distance(lines.begin(), a));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "last"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "first"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, AliasRejectsInvalidDuplicateAndReservedNames) {
    const SmashRunResult result = runSmash({
        "alias ll='echo ok'",
        "alias ll='echo duplicate'",
        "alias pwd='echo reserved'",
        "alias bad-name='echo bad'",
        "alias missing_quotes=echo",
        "quit"
    });

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: alias: ll already exists or is a reserved command\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: alias: pwd already exists or is a reserved command\n"));
    EXPECT_EQ(2, countOccurrences(result.stderrOutput,
                                  "smash error: alias: invalid alias format\n"));
}

TEST(SmashProcessIntegrationTest, UnaliasStopsAtFirstMissingAlias) {
    const SmashRunResult result = runSmash({
        "alias first='echo one'",
        "alias second='echo two'",
        "unalias first missing second",
        "alias",
        "quit"
    });

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_EQ(lines.end(), std::find(lines.begin(), lines.end(), "first='echo one'"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "second='echo two'"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unalias: missing alias does not exist\n"));
}

TEST(SmashProcessIntegrationTest, UnsetenvRemovesOnlyRequestedExistingVariables) {
    const SmashRunResult result = runSmash({
        "unsetenv SMASH_DROP",
        "/usr/bin/env",
        "quit"
    }, sourceRoot(), 5000, {{"SMASH_DROP", "gone"}, {"SMASH_KEEP", "still-here"}});

    expectExitedZero(result);
    const std::string envOutput = stripPrompts(result.stdoutOutput);
    EXPECT_FALSE(contains(envOutput, "SMASH_DROP="));
    EXPECT_TRUE(contains(envOutput, "SMASH_KEEP=still-here"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, UnsetenvStopsAtFirstMissingVariable) {
    const SmashRunResult result = runSmash({
        "unsetenv SMASH_FIRST SMASH_MISSING SMASH_KEEP",
        "/usr/bin/env",
        "quit"
    }, sourceRoot(), 5000, {{"SMASH_FIRST", "remove-me"}, {"SMASH_KEEP", "keep-me"}});

    expectExitedZero(result);
    const std::string envOutput = stripPrompts(result.stdoutOutput);
    EXPECT_FALSE(contains(envOutput, "SMASH_FIRST="));
    EXPECT_TRUE(contains(envOutput, "SMASH_KEEP=keep-me"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unsetenv: SMASH_MISSING does not exist\n"));
}

TEST(SmashProcessIntegrationTest, UnsetenvWithoutArgumentsIsAnError) {
    const SmashRunResult result = runSmash({"unsetenv", "quit"});

    expectExitedZero(result);
    EXPECT_EQ("smash error: unsetenv: not enough arguments\n", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, ExternalSimplePathForegroundWaitAndGlob) {
    test_support::TempDir temp;
    const std::string script = temp.makeExecutableScript(
        "delayed.sh",
        "#!/bin/sh\n"
        "/bin/sleep 0.2\n"
        "echo script-done\n"
    );
    temp.writeFile("b.txt", "b");
    temp.writeFile("a.txt", "a");

    const SmashRunResult result = runSmash({
        "/bin/echo simple",
        script,
        "/bin/echo *.txt",
        "quit"
    }, temp.root(), 4000);

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "simple"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "script-done"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "a.txt b.txt"));
    EXPECT_GE(result.elapsedMs, 180);
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, ExternalBackgroundDoesNotWait) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 3&",
        "quit kill"
    }, sourceRoot(), 2000);

    expectExitedZero(result);
    EXPECT_LT(result.elapsedMs, 1000);
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
}

TEST(SmashProcessIntegrationTest, RedirectionCreateTruncateAppendAndRestoreStdout) {
    test_support::TempDir temp;
    const std::string outFile = temp.writeFile("out.txt", "old\n");

    const SmashRunResult result = runSmash({
        "showpid > out.txt",
        "showpid >> out.txt",
        "/bin/echo external > ext.txt",
        "pwd",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(test_support::readFile(outFile),
                            "^smash pid is [0-9]+\nsmash pid is [0-9]+\n$"));
    EXPECT_EQ("external\n", test_support::readFile(temp.path("ext.txt")));
    EXPECT_EQ((std::vector<std::string>{temp.root()}), outputLines(result.stdoutOutput));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, RedirectionDoesNotLeakStdoutFds) {
    test_support::TempDir temp;
    SmashSession session(temp.root());
    session.sleepAndDrain(100);

    const int baseline = fdCount(session.pid());
    ASSERT_GT(baseline, 0);

    for (int index = 0; index < 40; ++index) {
        session.writeInput("pwd > leak_" + std::to_string(index) + ".txt\n");
    }
    session.sleepAndDrain(500);
    const int after = fdCount(session.pid());

    session.writeInput("quit\n");
    const SmashRunResult result = session.finish(3000);

    expectExitedZero(result);
    EXPECT_LE(after, baseline + 3) << result.stdoutOutput;
}


TEST(SmashProcessIntegrationTest, PipeDoesNotLeakStdoutFds)
{
    test_support::TempDir temp;
    SmashSession session(temp.root());
    session.sleepAndDrain(100);

    const int baseline = fdCount(session.pid());
    ASSERT_GT(baseline, 0);

    // ? runs on bash
    session.writeInput("(sleep 2 && echo test?)& | cat");

    session.sleepAndDrain(1500);
    const int after = fdCount(session.pid());

    session.writeInput("quit\n");
    const SmashRunResult result = session.finish(200);

    expectExitedZero(result);
    EXPECT_LE(after, baseline) << result.stdoutOutput;
}


TEST(SmashProcessIntegrationTest, PipesStdoutStderrAndNonDeadlock) {
    test_support::TempDir temp;
    const std::string script = temp.makeExecutableScript(
        "err_writer.sh",
        "#!/bin/sh\n"
        "echo stdout-line\n"
        "echo stderr-line >&2\n"
    );

    const SmashRunResult result = runSmash({
        "/bin/echo hello | /bin/grep hell",
        script + " |& /bin/grep stderr-line",
        "/usr/bin/yes x | /usr/bin/head -n 1",
        "quit"
    }, temp.root(), 4000, {}, {temp.root()});

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "hello"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "stderr-line"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "x"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, DuCountsNestedDirectoriesAndDoesNotFollowSymlinks) {
    test_support::TempDir temp;
    const std::string nested = temp.makeSubdir("nested");
    temp.writeFile("small.txt", std::string(10, 'a'));

    const std::string large = temp.writeFile("nested/large.txt", std::string(5000, 'b'));
    temp.makeSymlink(large, "large_link");

    const long expected = expectedDuKb(temp.root());
    const SmashRunResult result = runSmash({"du", "quit"}, temp.root());

    expectExitedZero(result);
    EXPECT_TRUE(contains(stripPrompts(result.stdoutOutput),
                         "Total disk usage: " + std::to_string(expected) + " KB"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, DuRejectsTooManyArguments) {
    const SmashRunResult result = runSmash({"du a b", "quit"});

    expectExitedZero(result);
    EXPECT_EQ("smash error: du: too many arguments\n", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, SysinfoOutputShape) {
    const SmashRunResult result = runSmash({"sysinfo ignored", "quit"});

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(
        stripPrompts(result.stdoutOutput),
        "System: .+\n"
        "Hostname: .+\n"
        "Kernel: .+\n"
        "Architecture: .+\n"
        "Boot Time: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\n"
    ));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, WhoamiOutputValues) {
    struct passwd* current = getpwuid(getuid());
    ASSERT_NE(nullptr, current);

    const SmashRunResult result = runSmash({"whoami ignored", "quit"});

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{
                  current->pw_name,
                  std::to_string(getuid()),
                  std::to_string(current->pw_gid),
                  current->pw_dir
              }),
              outputLines(result.stdoutOutput));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, CtrlCWithoutForegroundPrintsOnlyNotification) {
    SmashSession session(sourceRoot());
    session.sleepAndDrain(200);
    session.sendSignal(SIGINT);
    session.writeInput("quit\n");
    const SmashRunResult result = session.finish(3000);

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: got ctrl-C\n"));
    EXPECT_FALSE(regexSearch(result.stdoutOutput, "process [0-9]+ was killed"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, CtrlCKillsForegroundEvenIfItIgnoresSigint) {
    test_support::TempDir temp;
    const std::size_t slash = temp.root().find_last_of('/');
    const std::string marker = "smash_ignore_int_" + temp.root().substr(slash == std::string::npos ? 0 : slash + 1);
    const std::string scriptContents =
        std::string("#!/bin/bash\n") +
        "trap '' INT\n" +
        "exec -a " + marker + " /bin/sleep 20\n";
    const std::string script = temp.makeExecutableScript(
        "ignore_int.sh",
        scriptContents
    );

    SmashSession session(temp.root(), {}, {temp.root(), marker});
    session.writeInput(script + "\n");
    session.sleepAndDrain(400);
    session.sendSignal(SIGINT);
    session.writeInput("quit\n");
    const SmashRunResult result = session.finish(3000);

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(result.stdoutOutput,
                            "smash: got ctrl-C\nsmash: process [0-9]+ was killed\n"))
        << result.stdoutOutput;
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, CtrlCDoesNotKillBackgroundJob) {
    SmashSession session(sourceRoot());
    session.writeInput("/bin/sleep 5&\n");
    session.sleepAndDrain(200);
    session.sendSignal(SIGINT);
    session.writeInput("jobs\nquit kill\n");
    const SmashRunResult result = session.finish(3000);

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: got ctrl-C\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "[1] /bin/sleep 5&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashProcessIntegrationTest, StressMixedAliasesJobsRedirectionPipesAndSpaces) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "   alias hi='echo hello'   ",
        "hi",
        "hi > alias.txt",
        "hi | /bin/grep hell",
        "   /bin/sleep 5   &   ",
        "jobs",
        "kill -9 1",
        "/bin/sleep 0.1",
        "jobs",
        "unalias hi",
        "pwd >> alias.txt",
        "quit kill"
    }, temp.root(), 4000);

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "hello"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[1\\]\\s+/bin/sleep 5\\s*&\\s*\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 9 was sent to pid [0-9]+\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
    EXPECT_EQ(std::string("hello\n") + temp.root() + "\n", test_support::readFile(temp.path("alias.txt")));
    EXPECT_EQ("", result.stderrOutput);
}
