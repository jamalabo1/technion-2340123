#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "test_support.h"
#include "built_in.h"
#include "special.h"
#include "exceptions.h"

namespace
{
#ifdef __linux__

    void runDu(
        const std::shared_ptr<WorkingEnvironment>& wenv,
        const std::shared_ptr<Services>& services,
        const std::vector<std::string>& args
    )
    {
        CommandCtx ctx = test_support::makeCtx("du", wenv, services, args);
        DiskUsageCommand command(ctx);
        command.run();
    }

    void runUnsetEnvInChildAndExpectMissingVariable(const std::string& name)
    {
        pid_t pid = fork();
        ASSERT_NE(-1, pid);
        if (pid == 0)
        {
            auto services = test_support::makeServices();
            auto wenv = test_support::makeWenv();
            CommandCtx ctx = test_support::makeCtx("unsetenv", wenv, services, {name});
            UnSetEnvCommand command(ctx);
            try
            {
                command.run();
            }
            catch (const CommandDomainError& e)
            {
                _exit(std::string(e.what()) == name + " does not exist" ? 0 : 2);
            } catch (...)
            {
                _exit(3);
            }
            _exit(4);
        }

        int status = 0;
        ASSERT_EQ(pid, waitpid(pid, &status, 0));
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(0, WEXITSTATUS(status));
    }

#endif
} // namespace

TEST(DiskUsageCommandTest, NoPathUsesCurrentDirectory)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;
    temp.writeFile("one_byte.txt", "x");

    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto runNoArg = test_support::runInChildCapturingStdout([]() {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {});
    });

    auto runExplicit = test_support::runInChildCapturingStdout([&]() {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {temp.root()});
    });

    ASSERT_TRUE(WIFEXITED(runNoArg.status));
    ASSERT_TRUE(WIFEXITED(runExplicit.status));
    ASSERT_EQ(0, WEXITSTATUS(runNoArg.status));
    ASSERT_EQ(0, WEXITSTATUS(runExplicit.status));

    EXPECT_EQ(runExplicit.stdoutOutput, runNoArg.stdoutOutput);
#endif
}


TEST(DiskUsageCommandTest, RejectsTooManyArguments)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    EXPECT_THROW(runDu(wenv, services, {"/tmp", "/var"}), CommandTooManyArguments);
#endif
}

TEST(UnSetEnvCommandTest, RequiresAtLeastOneVariableName)
{
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    CommandCtx ctx = test_support::makeCtx("unsetenv", wenv, services, {});
    UnSetEnvCommand command(ctx);

    EXPECT_THROW(command.run(), CommandNotEnoughArguments);
}

TEST(UnSetEnvCommandTest, ReportsFirstMissingVariableWithoutTouchingParentEnvironment)
{
#ifndef __linux__
    GTEST_SKIP() << "unsetenv command scans Linux /proc";
#else
    runUnsetEnvInChildAndExpectMissingVariable("SMASH_GTEST_VARIABLE_THAT_SHOULD_NOT_EXIST");
#endif
}
