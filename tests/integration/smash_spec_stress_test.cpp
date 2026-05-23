#include <gtest/gtest.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.h"

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
    const std::string suffix = "/tests/integration/smash_spec_stress_test.cpp";
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

    char exePath[4096];
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool regexSearch(const std::string& text, const std::string& pattern) {
    return std::regex_search(text, std::regex(pattern));
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

std::string joinPath(const std::string& base, const std::string& child) {
    if (base.empty() || base[base.size() - 1] == '/') {
        return base + child;
    }
    return base + "/" + child;
}

std::string readTextFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open " + path);
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl failed");
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

            if (count > 0) {
                poll(fds, count, 5);
            } else {
                usleep(5000);
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
    return (expectedDuBlocks(path) + 1) / 2;
}

} // namespace

TEST(SmashSpecBuiltinsStressTest, ChpromptShowpidPwdAndBuiltInAmpersandSemantics) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "chprompt spec ignored",
        "pwd extra args",
        "showpid ignored &",
        "chprompt",
        "pwd &",
        "jobs ignored",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput, {"smash", "spec"});
    ASSERT_EQ(3u, lines.size()) << result.stdoutOutput;
    EXPECT_EQ(temp.root(), lines[0]);
    EXPECT_TRUE(regexSearch(lines[1], "^smash pid is [0-9]+$"));
    EXPECT_EQ(temp.root(), lines[2]);
    EXPECT_FALSE(contains(result.stdoutOutput, "[1]"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecBuiltinsStressTest, CdRelativeAbsoluteDashNoopAndErrorStreams) {
    test_support::TempDir temp;
    const std::string child = temp.makeSubdir("child");

    const SmashRunResult result = runSmash({
        "pwd",
        "cd",
        "pwd",
        "cd child",
        "pwd",
        "cd " + temp.root(),
        "pwd",
        "cd -",
        "pwd",
        "cd missing",
        "cd a b",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{temp.root(), temp.root(), child, temp.root(), child}),
              outputLines(result.stdoutOutput));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: chdir failed: "));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: cd: too many arguments\n"));
}

TEST(SmashSpecBuiltinsStressTest, CdDashWithUnsetOldpwdIsAnErrorAndDoesNotMove) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "cd -",
        "pwd",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{temp.root()}), outputLines(result.stdoutOutput));
    EXPECT_EQ("smash error: cd: OLDPWD not set\n", result.stderrOutput);
}

TEST(SmashSpecJobsForegroundQuitStressTest, JobsOrderingFinishedRemovalAndQuitKillFormatting) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 3&",
        "/bin/sleep 3 &",
        "/bin/true&",
        "/bin/sleep 0.2",
        "jobs ignored",
        "quit kill"
    }, sourceRoot(), 5000);

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[1\\] /bin/sleep 3&\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[2\\] /bin/sleep 3 &\n"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[3] /bin/true&"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 2 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecJobsForegroundQuitStressTest, FgExplicitJobIdWaitsAndRemovesOnlyThatJob) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 1&",
        "/bin/sleep 3&",
        "fg 1",
        "jobs",
        "quit kill"
    }, sourceRoot(), 6000);

    expectExitedZero(result);
    EXPECT_GE(result.elapsedMs, 900);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "/bin/sleep 1& [0-9]+\n"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1] /bin/sleep 1&"));
    EXPECT_TRUE(contains(result.stdoutOutput, "[2] /bin/sleep 3&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecJobsForegroundQuitStressTest, FgDefaultAndErrorCasesAreSeparatedToStderr) {
    const SmashRunResult result = runSmash({
        "fg",
        "fg abc",
        "fg -2",
        "fg 1 2",
        "fg 99",
        "quit"
    });

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: fg: jobs list is empty\n"));
    EXPECT_EQ(3, countOccurrences(result.stderrOutput, "smash error: fg: invalid arguments\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: fg: job-id 99 does not exist\n"));
}

TEST(SmashSpecJobsForegroundQuitStressTest, QuitKillWithNoJobsHasExactOutput) {
    const SmashRunResult result = runSmash({"quit kill"});

    expectExitedZero(result);
    EXPECT_EQ(std::string(kPrompt) + "smash: sending SIGKILL signal to 0 jobs:\n",
              result.stdoutOutput);
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecKillStressTest, ValidInvalidMissingAndInvalidSignalCases) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 4&",
        "kill -999999 1",
        "kill 9 1",
        "kill -x 1",
        "kill -9 99",
        "kill -9 1",
        "/bin/sleep 0.2",
        "jobs",
        "quit kill"
    }, sourceRoot(), 5000);

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: kill failed: "));
    EXPECT_EQ(2, countOccurrences(result.stderrOutput, "smash error: kill: invalid arguments\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: kill: job-id 99 does not exist\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 9 was sent to pid [0-9]+\n"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1] /bin/sleep 4&"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
}

TEST(SmashSpecAliasUnaliasStressTest, AliasExpansionOnlyFirstWordAndWorksInsidePipeAndRedirection) {
    test_support::TempDir temp;
    const std::string aliasFile = temp.writeFile("alias.txt", "old\n");

    const SmashRunResult result = runSmash({
        "alias say='echo aliased'",
        "say",
        "/bin/echo say",
        "say > alias.txt",
        "say | /bin/grep aliased",
        "/bin/echo say | /bin/grep say",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "aliased"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "say"));
    EXPECT_EQ("aliased\n", test_support::readFile(aliasFile));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecAliasUnaliasStressTest, AliasValidationDuplicateReservedListingAndUnaliasStop) {
    const SmashRunResult result = runSmash({
        "alias first='echo one'",
        "alias second='echo two'",
        "alias first='echo duplicate'",
        "alias pwd='echo reserved'",
        "alias bad-name='echo bad'",
        "alias missing_quotes=echo",
        "unalias first missing second",
        "alias",
        "quit"
    });

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_EQ(lines.end(), std::find(lines.begin(), lines.end(), "first='echo one'"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "second='echo two'"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: alias: first already exists or is a reserved command\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: alias: pwd already exists or is a reserved command\n"));
    EXPECT_EQ(2, countOccurrences(result.stderrOutput,
                                  "smash error: alias: invalid alias format\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unalias: missing alias does not exist\n"));
}

TEST(SmashSpecUnsetenvStressTest, RemovesMultipleVariablesAndStopsAtFirstMissingName) {
    const SmashRunResult result = runSmash({
        "unsetenv SMASH_DROP1 SMASH_DROP2 SMASH_MISSING SMASH_KEEP",
        "/usr/bin/env",
        "quit"
    }, sourceRoot(), 5000, {
        {"SMASH_DROP1", "one"},
        {"SMASH_DROP2", "two"},
        {"SMASH_KEEP", "keep-me"}
    });

    expectExitedZero(result);
    const std::string envOutput = stripPrompts(result.stdoutOutput);
    EXPECT_FALSE(contains(envOutput, "SMASH_DROP1="));
    EXPECT_FALSE(contains(envOutput, "SMASH_DROP2="));
    EXPECT_TRUE(contains(envOutput, "SMASH_KEEP=keep-me"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unsetenv: SMASH_MISSING does not exist\n"));
}

TEST(SmashSpecExternalStressTest, PathLookupArgumentPreservationForegroundWaitAndCommandNotFound) {
    test_support::TempDir temp;
    temp.makeExecutableScript(
        "argdump",
        "#!/bin/sh\n"
        "/bin/sleep 0.2\n"
        "printf '<%s>\\n' \"$0\" \"$1\" \"$2\" \"$3\"\n"
    );

    const SmashRunResult result = runSmash({
        "argdump one two three",
        "definitely_missing_smash_command",
        "pwd",
        "quit"
    }, temp.root(), 5000, {{"PATH", temp.root() + ":/usr/bin:/bin"}});

    expectExitedZero(result);
    EXPECT_GE(result.elapsedMs, 180);
    EXPECT_TRUE(contains(result.stdoutOutput, "<" + temp.root() + "/argdump>\n<one>\n<two>\n<three>\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, temp.root() + "\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: execvp failed: "));
}

TEST(SmashSpecExternalStressTest, BackgroundTrailingAmpersandFormsDoNotWaitAndAreJobs) {
    const SmashRunResult result = runSmash({
        "/bin/sleep 4&",
        "/bin/sleep 4 &",
        "jobs",
        "quit kill"
    }, sourceRoot(), 2000);

    expectExitedZero(result);
    EXPECT_LT(result.elapsedMs, 1200);
    EXPECT_TRUE(contains(result.stdoutOutput, "[1] /bin/sleep 4&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "[2] /bin/sleep 4 &\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 2 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecExternalStressTest, ComplexWildcardAndQuestionMarkAreHandledByBash) {
    test_support::TempDir temp;
    const std::string outFile = temp.writeFile("glob.txt", "old\n");
    temp.writeFile("xa", "a");
    temp.writeFile("xb", "b");

    const SmashRunResult result = runSmash({
        "/bin/echo x? > glob.txt",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ("xa xb\n", test_support::readFile(outFile));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecRedirectionStressTest, CreateTruncateAppendBuiltinsExternalsAndRestoreStdout) {
    test_support::TempDir temp;
    const std::string outFile = temp.writeFile("out.txt", "old\n");
    const std::string extFile = temp.writeFile("ext.txt", "stale\n");

    const SmashRunResult result = runSmash({
        "showpid > out.txt",
        "showpid >> out.txt",
        "/bin/echo external > ext.txt",
        "pwd > /no/such/dir/file",
        "pwd",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(test_support::readFile(outFile),
                            "^smash pid is [0-9]+\nsmash pid is [0-9]+\n$"));
    EXPECT_EQ("external\n", test_support::readFile(extFile));
    EXPECT_EQ((std::vector<std::string>{temp.root()}), outputLines(result.stdoutOutput));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: open failed: "));
}

TEST(SmashSpecRedirectionStressTest, RedirectionDisablesBackgroundAndDoesNotAddBuiltinJobs) {
    test_support::TempDir temp;
    const std::string outFile = temp.writeFile("pid.txt", "");

    const SmashRunResult result = runSmash({
        "showpid > pid.txt &",
        "jobs",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_TRUE(regexSearch(test_support::readFile(outFile), "^smash pid is [0-9]+\n$"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1]"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecPipeStressTest, PipesStdoutStderrBuiltinsExternalsAndDoesNotHang) {
    test_support::TempDir temp;
    const std::string script = temp.makeExecutableScript(
        "mixed.sh",
        "#!/bin/sh\n"
        "echo stdout-line\n"
        "echo stderr-line >&2\n"
    );

    const SmashRunResult result = runSmash({
        "showpid | /bin/grep pid",
        "/bin/echo ignored | pwd",
        script + " |& /bin/grep stderr-line",
        "/usr/bin/yes x | /usr/bin/head -n 1",
        "quit"
    }, temp.root(), 5000, {}, {temp.root()});

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return regexSearch(line, "^smash pid is [0-9]+$");
    }));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), temp.root()));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "stderr-line"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "x"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecSignalsStressTest, CtrlCNoForegroundForegroundAndBackgroundIsolation) {
    test_support::TempDir temp;
    const std::string marker = "smash_spec_ignore_int_" + temp.root().substr(temp.root().find_last_of('/') + 1);
    const std::string script = temp.makeExecutableScript(
        "ignore_int.sh",
        std::string("#!/bin/bash\n") +
        "trap '' INT\n"
        "exec -a " + marker + " /bin/sleep 20\n"
    );

    SmashSession session(temp.root(), {}, {temp.root(), marker});
    session.sleepAndDrain(100);
    session.sendSignal(SIGINT);
    session.writeInput("/bin/sleep 5&\n");
    session.sleepAndDrain(100);
    session.writeInput(script + "\n");
    session.sleepAndDrain(400);
    session.sendSignal(SIGINT);
    session.writeInput("jobs\nquit kill\n");
    const SmashRunResult result = session.finish(5000);

    expectExitedZero(result);
    EXPECT_EQ(2, countOccurrences(result.stdoutOutput, "smash: got ctrl-C\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "smash: process [0-9]+ was killed\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "[1] /bin/sleep 5&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecSpecialCommandsStressTest, SysinfoMatchesKernelBackedFields) {
    struct utsname unameBuf {};
    ASSERT_EQ(0, uname(&unameBuf));

    const std::string ostype = readTextFile("/proc/sys/kernel/ostype");
    const std::string hostname = readTextFile("/proc/sys/kernel/hostname");
    const std::string release = readTextFile("/proc/sys/kernel/osrelease");

    const SmashRunResult result = runSmash({"sysinfo ignored", "quit"});

    expectExitedZero(result);
    const std::string output = stripPrompts(result.stdoutOutput);
    EXPECT_TRUE(contains(output, "System: " + ostype));
    EXPECT_TRUE(contains(output, "Hostname: " + hostname));
    EXPECT_TRUE(contains(output, "Kernel: " + release));
    EXPECT_TRUE(contains(output, std::string("Architecture: ") + unameBuf.machine + "\n"));
    EXPECT_TRUE(regexSearch(output, "Boot Time: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecSpecialCommandsStressTest, DuDefaultExplicitNestedAndSymlinkAccounting) {
    test_support::TempDir temp;
    const std::string nested = temp.makeSubdir("nested");
    temp.writeFile("root_file.txt", std::string(100, 'r'));
    const std::string nestedFile = temp.writeFile("nested/nested_file.txt", std::string(5000, 'n'));
    temp.makeSymlink(nestedFile, "nested_link");

    const long expectedRoot = expectedDuKb(temp.root());
    const long expectedNested = expectedDuKb(nested);

    const SmashRunResult result = runSmash({
        "du",
        "du nested",
        "du a b",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    const std::string output = stripPrompts(result.stdoutOutput);
    EXPECT_TRUE(contains(output, "Total disk usage: " + std::to_string(expectedRoot) + " KB\n"));
    EXPECT_TRUE(contains(output, "Total disk usage: " + std::to_string(expectedNested) + " KB\n"));
    EXPECT_EQ("smash error: du: too many arguments\n", result.stderrOutput);
}

TEST(SmashSpecSpecialCommandsStressTest, WhoamiUsesPasswdValuesAndIgnoresArgs) {
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

TEST(SmashSpecExpandedBuiltinsMatrixTest, ChpromptShowpidAndPwdAdditionalEdges) {
    test_support::TempDir temp;
    const std::string pidFile = temp.writeFile("pid.txt", "old\n");
    const std::string pwdFile = temp.writeFile("pwd.txt", "old\n");

    const SmashRunResult result = runSmash({
        "chprompt alpha",
        "showpid",
        "pwd",
        "chprompt beta ignored1 ignored2",
        "showpid ignored args",
        "pwd ignored args",
        "chprompt",
        "showpid &",
        "pwd &",
        "chprompt z9_",
        "showpid > pid.txt",
        "showpid >> pid.txt",
        "pwd > pwd.txt",
        "pwd >> pwd.txt",
        "jobs",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput, {"smash", "alpha", "beta", "z9_"});
    ASSERT_EQ(6u, lines.size()) << result.stdoutOutput;
    EXPECT_TRUE(regexSearch(lines[0], "^smash pid is [0-9]+$"));
    EXPECT_EQ(temp.root(), lines[1]);
    EXPECT_TRUE(regexSearch(lines[2], "^smash pid is [0-9]+$"));
    EXPECT_EQ(temp.root(), lines[3]);
    EXPECT_TRUE(regexSearch(lines[4], "^smash pid is [0-9]+$"));
    EXPECT_EQ(temp.root(), lines[5]);
    EXPECT_TRUE(regexSearch(test_support::readFile(pidFile),
                            "^smash pid is [0-9]+\nsmash pid is [0-9]+\n$"));
    EXPECT_EQ(temp.root() + "\n" + temp.root() + "\n", test_support::readFile(pwdFile));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1]"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecExpandedCdMatrixTest, AdditionalPathFormsAndFailuresDoNotMutateCwd) {
    test_support::TempDir temp;
    const std::string a = temp.makeSubdir("a");
    const std::string nested = temp.makeSubdir("a/nested");

    const SmashRunResult result = runSmash({
        "pwd",
        "cd .",
        "pwd",
        "cd a",
        "pwd",
        "cd ..",
        "pwd",
        "cd ./a/nested",
        "pwd",
        "cd ../..",
        "pwd",
        "cd /",
        "pwd",
        "cd " + temp.root(),
        "pwd",
        "cd a",
        "cd -",
        "pwd",
        "cd -",
        "pwd",
        "cd missing",
        "pwd",
        "cd a b",
        "pwd",
        "quit"
    }, temp.root());

    expectExitedZero(result);
    EXPECT_EQ((std::vector<std::string>{
                  temp.root(),
                  temp.root(),
                  a,
                  temp.root(),
                  nested,
                  temp.root(),
                  "/",
                  temp.root(),
                  temp.root(),
                  a,
                  a,
                  a
              }),
              outputLines(result.stdoutOutput));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: chdir failed: "));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: cd: too many arguments\n"));
}

TEST(SmashSpecExpandedJobControlMatrixTest, JobsFgKillAndQuitAdditionalEdges) {
    const SmashRunResult result = runSmash({
        "jobs ignored",
        "/bin/sleep 1&",
        "jobs",
        "fg 1",
        "jobs",
        "/bin/sleep 4&",
        "/bin/sleep 4 &",
        "jobs ignored",
        "kill -0 1",
        "kill -15 2",
        "/bin/sleep 0.2",
        "jobs",
        "kill -9 1",
        "/bin/sleep 0.2",
        "jobs",
        "quit kill"
    }, sourceRoot(), 5000);

    expectExitedZero(result);
    EXPECT_GE(result.elapsedMs, 900);
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[1\\] /bin/sleep 1&\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "/bin/sleep 1& [0-9]+\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[1\\] /bin/sleep 4&\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "\\[2\\] /bin/sleep 4 &\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 0 was sent to pid [0-9]+\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 15 was sent to pid [0-9]+\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "signal number 9 was sent to pid [0-9]+\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}

TEST(SmashSpecExpandedQuitMatrixTest, AdditionalQuitFormsAndKillOutput) {
    {
        const SmashRunResult result = runSmash({"quit"});
        expectExitedZero(result);
        EXPECT_EQ(kPrompt, result.stdoutOutput);
        EXPECT_EQ("", result.stderrOutput);
    }
    {
        const SmashRunResult result = runSmash({"quit ignored"});
        expectExitedZero(result);
        EXPECT_EQ(kPrompt, result.stdoutOutput);
        EXPECT_EQ("", result.stderrOutput);
    }
    {
        const SmashRunResult result = runSmash({"chprompt bye", "quit kill"});
        expectExitedZero(result);
        EXPECT_EQ(std::string(kPrompt) + "bye> bye: sending SIGKILL signal to 0 jobs:\n",
                  result.stdoutOutput);
        EXPECT_EQ("", result.stderrOutput);
    }
    {
        const SmashRunResult result = runSmash({"/bin/sleep 4&", "quit kill extra"}, sourceRoot(), 2000);
        expectExitedZero(result);
        EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 1 jobs:\n"));
        EXPECT_TRUE(regexSearch(result.stdoutOutput, "[0-9]+: /bin/sleep 4&\n"));
        EXPECT_EQ("", result.stderrOutput);
    }
    {
        const SmashRunResult result = runSmash({"/bin/true&", "/bin/sleep 0.2", "quit kill"},
                                               sourceRoot(), 3000);
        expectExitedZero(result);
        EXPECT_TRUE(contains(result.stdoutOutput, "smash: sending SIGKILL signal to 0 jobs:\n"));
    }
    {
        test_support::TempDir temp;
        const std::string output = temp.writeFile("quit.txt", "old\n");
        const SmashRunResult result = runSmash({"quit > quit.txt"}, temp.root());
        expectExitedZero(result);
        EXPECT_EQ(kPrompt, result.stdoutOutput);
        EXPECT_EQ("", result.stderrOutput);
        EXPECT_EQ("", test_support::readFile(output));
    }
}

TEST(SmashSpecExpandedAliasMatrixTest, AdditionalAliasAndUnaliasEdges) {
    const SmashRunResult result = runSmash({
        "alias a1='echo one'",
        "alias _under='echo under'",
        "alias e=''",
        "alias a1_suffix='echo suffix'",
        "alias",
        "a1 extra args",
        "/bin/echo a1",
        "_under",
        "alias e='echo nope'",
        "unalias _under a1_suffix",
        "alias",
        "_under",
        "a1_suffix",
        "unalias",
        "unalias a1 missing e",
        "alias",
        "quit"
    });

    expectExitedZero(result);
    const std::vector<std::string> lines = outputLines(result.stdoutOutput);
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "a1='echo one'"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "_under='echo under'"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "e=''"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "a1_suffix='echo suffix'"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "one extra args"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "a1"));
    EXPECT_NE(lines.end(), std::find(lines.begin(), lines.end(), "under"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: alias: e already exists or is a reserved command\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unalias: not enough arguments\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unalias: missing alias does not exist\n"));
    EXPECT_EQ(2, countOccurrences(result.stderrOutput, "smash error: execvp failed: "));
}

TEST(SmashSpecExpandedUnsetenvMatrixTest, AdditionalEnvironmentMutationEdges) {
    test_support::TempDir temp;

    const SmashRunResult result = runSmash({
        "/usr/bin/env > before.txt",
        "unsetenv SMASH_EDGE_A",
        "/usr/bin/env > after_a.txt",
        "unsetenv SMASH_EDGE_B SMASH_EDGE_C",
        "/usr/bin/env > after_bc.txt",
        "unsetenv SMASH_EDGE_MISSING SMASH_EDGE_D",
        "/usr/bin/env > after_missing.txt",
        "unsetenv",
        "unsetenv SMASH_EDGE_D SMASH_EDGE_EMPTY",
        "/usr/bin/env > final.txt",
        "quit"
    }, temp.root(), 5000, {
        {"SMASH_EDGE_A", "one"},
        {"SMASH_EDGE_B", "two"},
        {"SMASH_EDGE_C", "three"},
        {"SMASH_EDGE_D", "four"},
        {"SMASH_EDGE_EMPTY", ""}
    });

    expectExitedZero(result);
    const std::string before = test_support::readFile(temp.path("before.txt"));
    const std::string afterA = test_support::readFile(temp.path("after_a.txt"));
    const std::string afterBc = test_support::readFile(temp.path("after_bc.txt"));
    const std::string afterMissing = test_support::readFile(temp.path("after_missing.txt"));
    const std::string finalEnv = test_support::readFile(temp.path("final.txt"));

    EXPECT_TRUE(contains(before, "SMASH_EDGE_A=one"));
    EXPECT_TRUE(contains(before, "SMASH_EDGE_EMPTY="));
    EXPECT_FALSE(contains(afterA, "SMASH_EDGE_A="));
    EXPECT_TRUE(contains(afterA, "SMASH_EDGE_B=two"));
    EXPECT_FALSE(contains(afterBc, "SMASH_EDGE_B="));
    EXPECT_FALSE(contains(afterBc, "SMASH_EDGE_C="));
    EXPECT_TRUE(contains(afterBc, "SMASH_EDGE_D=four"));
    EXPECT_TRUE(contains(afterMissing, "SMASH_EDGE_D=four"));
    EXPECT_TRUE(contains(afterMissing, "SMASH_EDGE_EMPTY="));
    EXPECT_FALSE(contains(finalEnv, "SMASH_EDGE_D="));
    EXPECT_FALSE(contains(finalEnv, "SMASH_EDGE_EMPTY="));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unsetenv: SMASH_EDGE_MISSING does not exist\n"));
    EXPECT_TRUE(contains(result.stderrOutput,
                         "smash error: unsetenv: not enough arguments\n"));
}

TEST(SmashSpecExpandedExternalIoMatrixTest, AdditionalExternalRedirectionAndPipeEdges) {
    test_support::TempDir temp;
    const std::string script = temp.makeExecutableScript(
        "stdio.sh",
        "#!/bin/sh\n"
        "echo argc:$#\n"
        "for arg in \"$@\"; do echo arg:$arg; done\n"
        "echo OUT_EDGE\n"
        "echo ERR_EDGE >&2\n"
    );
    temp.writeFile("fileA", "a");
    temp.writeFile("fileB", "b");
    const std::string append = temp.writeFile("append.txt", "");
    const std::string stdoutFile = temp.writeFile("stdout.txt", "");
    const std::string bgFile = temp.writeFile("bg.txt", "");
    const std::string globFile = temp.writeFile("glob.txt", "");

    const SmashRunResult result = runSmash({
        script + " a b c",
        script + " x y > stdout.txt",
        script + " p | /bin/grep OUT_EDGE",
        script + " p |& /bin/grep ERR_EDGE",
        "/bin/echo first > append.txt",
        "/bin/echo second >> append.txt",
        "/bin/echo bgred > bg.txt &",
        "jobs",
        "/bin/echo fail > /no/such-dir/out",
        "/bin/echo RESTORED",
        "definitely_missing_edge_command",
        "/bin/echo file* > glob.txt",
        "quit"
    }, temp.root(), 5000, {}, {temp.root()});

    expectExitedZero(result);
    EXPECT_TRUE(contains(result.stdoutOutput, "argc:3\narg:a\narg:b\narg:c\nOUT_EDGE\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "OUT_EDGE\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "ERR_EDGE\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "RESTORED\n"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1]"));
    EXPECT_TRUE(contains(result.stderrOutput, "ERR_EDGE\n"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: open failed: "));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: execvp failed: "));
    EXPECT_TRUE(contains(test_support::readFile(stdoutFile), "argc:2\narg:x\narg:y\nOUT_EDGE\n"));
    EXPECT_EQ("first\nsecond\n", test_support::readFile(append));
    EXPECT_EQ("bgred\n", test_support::readFile(bgFile));
    EXPECT_EQ("fileA fileB\n", test_support::readFile(globFile));
}

TEST(SmashSpecExpandedSpecialCommandsMatrixTest, AdditionalSysinfoDuWhoamiEdges) {
    test_support::TempDir temp;
    const std::string nested = temp.makeSubdir("nested");
    temp.writeFile("root.txt", std::string(128, 'r'));
    temp.writeFile("nested/child.txt", std::string(4096, 'c'));

    struct passwd* current = getpwuid(getuid());
    ASSERT_NE(nullptr, current);

    const long nestedKb = expectedDuKb(nested);

    const SmashRunResult result = runSmash({
        "sysinfo > sysinfo.txt",
        "sysinfo ignored | /bin/grep Kernel:",
        "sysinfo &",
        "du > du_cwd.txt",
        "du nested > du_nested.txt",
        "du . | /bin/grep Total",
        "du missing",
        "du nested extra",
        "whoami > whoami.txt",
        "whoami ignored | /usr/bin/head -n 1",
        "whoami &",
        "jobs",
        "quit"
    }, temp.root(), 5000);

    expectExitedZero(result);
    const std::string sysinfo = test_support::readFile(temp.path("sysinfo.txt"));
    EXPECT_TRUE(contains(sysinfo, "System: "));
    EXPECT_TRUE(contains(sysinfo, "Hostname: "));
    EXPECT_TRUE(contains(sysinfo, "Kernel: "));
    EXPECT_TRUE(contains(sysinfo, "Architecture: "));
    EXPECT_TRUE(contains(sysinfo, "Boot Time: "));
    EXPECT_TRUE(contains(result.stdoutOutput, "Kernel: "));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "Total disk usage: [0-9]+ KB\n"));
    EXPECT_TRUE(regexSearch(test_support::readFile(temp.path("du_cwd.txt")),
                            "^Total disk usage: [0-9]+ KB\n$"));
    EXPECT_EQ("Total disk usage: " + std::to_string(nestedKb) + " KB\n",
              test_support::readFile(temp.path("du_nested.txt")));
    EXPECT_EQ(std::string(current->pw_name) + "\n" +
                  std::to_string(getuid()) + "\n" +
                  std::to_string(current->pw_gid) + "\n" +
                  current->pw_dir + "\n",
              test_support::readFile(temp.path("whoami.txt")));
    EXPECT_TRUE(contains(result.stdoutOutput, std::string(current->pw_name) + "\n"));
    EXPECT_FALSE(contains(result.stdoutOutput, "[1]"));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: open failed: "));
    EXPECT_TRUE(contains(result.stderrOutput, "smash error: du: too many arguments\n"));
}

TEST(SmashSpecExpandedSignalsMatrixTest, AdditionalCtrlCEdges) {
    test_support::TempDir temp;

    SmashSession session(temp.root());
    session.writeInput("chprompt sig\n");
    session.sleepAndDrain(100);
    session.sendSignal(SIGINT);
    session.sendSignal(SIGINT);
    session.writeInput("/bin/sleep 5&\n");
    session.sleepAndDrain(100);
    session.sendSignal(SIGINT);
    session.writeInput("/bin/sleep 20\n");
    session.sleepAndDrain(300);
    session.sendSignal(SIGINT);
    session.writeInput("pwd\njobs\nquit kill\n");
    const SmashRunResult result = session.finish(5000);

    expectExitedZero(result);
    EXPECT_EQ(4, countOccurrences(result.stdoutOutput, "sig: got ctrl-C\n"));
    EXPECT_TRUE(regexSearch(result.stdoutOutput, "sig: process [0-9]+ was killed\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, temp.root() + "\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "[1] /bin/sleep 5&\n"));
    EXPECT_TRUE(contains(result.stdoutOutput, "sig: sending SIGKILL signal to 1 jobs:\n"));
    EXPECT_EQ("", result.stderrOutput);
}
