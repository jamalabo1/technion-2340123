#include <gtest/gtest.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "../test_support.h"
#include "commands/external.h"
#include "shell.h"

namespace {

void runOwned(Command* command) {
    if (command == nullptr) {
        throw std::runtime_error("CreateCommand returned null");
    }
    std::unique_ptr<Command> owned(command);
    command->run();
}

} // namespace

TEST(ExternalCommandTest, ForegroundSimpleCommandExecsAndWaits) {
    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([]() {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        CommandCtx ctx = test_support::makeCtx("/bin/echo", wenv, services, {"hello"});
        ExternalCommand command(ctx);

        command.run();

        if (command.get_proc() == nullptr) {
            throw std::runtime_error("foreground command did not record process");
        }
        if (services->proc_management->getFgProc() != nullptr) {
            throw std::runtime_error("foreground process was not cleared");
        }
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("hello\n", result.stdoutOutput);
}

TEST(ExternalCommandTest, BackgroundCommandReturnsWithoutWaiting) {
    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([]() {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        CommandCtx ctx = test_support::makeCtx("/bin/sleep", wenv, services, {"30"}, true);
        ExternalCommand command(ctx);

        command.run();

        if (command.get_proc() == nullptr) {
            throw std::runtime_error("background command did not record process");
        }
        if (services->proc_management->getFgProc() != nullptr) {
            throw std::runtime_error("background command set foreground process");
        }

        const pid_t pid = command.get_proc()->pId;
        if (kill(pid, SIGKILL) == -1) {
            throw std::runtime_error("kill failed");
        }
        int status = 0;
        if (waitpid(pid, &status, 0) != pid || !WIFSIGNALED(status)) {
            throw std::runtime_error("background child was not killed and reaped");
        }
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    EXPECT_EQ(0, WEXITSTATUS(result.status));
}

TEST(ExternalCommandTest, ComplexCommandUsesBashForWildcardExpansion) {
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;
    temp.writeFile("alpha.txt", "a");
    temp.writeFile("beta.txt", "b");
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([]() {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        CommandCtx ctx = test_support::makeCtx("/bin/echo", wenv, services, {"*.txt"});
        ExternalCommand command(ctx);
        command.run();
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("alpha.txt beta.txt\n", result.stdoutOutput);
}

TEST(PipeCommandTest, PipesStdoutFromFirstCommandIntoSecondCommand) {
    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([]() {
        runOwned(SmallShell::getInstance().CreateCommand("/bin/echo hello | /usr/bin/grep hell"));
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("hello\n", result.stdoutOutput);
}

TEST(PipeCommandTest, PipeAmpersandPipesStderrFromFirstCommandIntoSecondCommand) {
    test_support::TempDir temp;
    const std::string script = temp.makeExecutableScript(
        "write_stderr.sh",
        "#!/bin/sh\n"
        "echo stdout-line\n"
        "echo stderr-line >&2\n"
    );

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]() {
        runOwned(SmallShell::getInstance().CreateCommand((script + " |& /usr/bin/grep stderr-line").c_str()));
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("stderr-line\n", result.stdoutOutput);
}

TEST(RedirectionCommandTest, WriteRedirectionCreatesOrTruncatesOutputFile) {
    test_support::TempDir temp;
    const std::string outputPath = temp.writeFile("pid.txt", "old contents\n");

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]() {
        runOwned(SmallShell::getInstance().CreateCommand(("showpid > " + outputPath).c_str()));
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("smash pid is " + std::to_string(result.pid) + "\n", test_support::readFile(outputPath));
}

TEST(RedirectionCommandTest, AppendRedirectionAppendsToExistingFile) {
    test_support::TempDir temp;
    const std::string outputPath = temp.writeFile("pid.txt", "prefix\n");

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]() {
        runOwned(SmallShell::getInstance().CreateCommand(("showpid >> " + outputPath).c_str()));
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("prefix\nsmash pid is " + std::to_string(result.pid) + "\n", test_support::readFile(outputPath));
}
