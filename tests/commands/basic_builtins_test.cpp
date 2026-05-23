#include <gtest/gtest.h>

#include <pwd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <vector>

#include "test_support.h"
#include "built_in.h"
#include "special.h"

namespace {

template <typename CommandT>
void runCommand(
    const std::string& commandId,
    const std::vector<std::string>& args,
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services
) {
    CommandCtx ctx = test_support::makeCtx(commandId, wenv, services, args);
    CommandT command(ctx);
    command.run();
}

} // namespace

TEST(ChangeTerminalNameCommandTest, SetsFirstArgumentAndIgnoresTheRest) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    runCommand<ChangeTerminalNameCommand>("chprompt", {"custom", "ignored"}, wenv, services);

    EXPECT_EQ("custom", wenv->getTerminalName());
}

TEST(ChangeTerminalNameCommandTest, ResetsToAppNameWhenNoArgumentsAreGiven) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    wenv->setTerminalName("custom");

    runCommand<ChangeTerminalNameCommand>("chprompt", {}, wenv, services);

    EXPECT_EQ("smash", wenv->getTerminalName());
}

TEST(GetCurrDirCommandTest, PrintsCurrentWorkingDirectoryAndIgnoresArguments) {
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;
    ASSERT_EQ(0, chdir(temp.root().c_str()));
    const std::string expectedCwd = test_support::getCwd();
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    testing::internal::CaptureStdout();
    runCommand<GetCurrDirCommand>("pwd", {"ignored"}, wenv, services);
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(expectedCwd + "\n", output);
}

TEST(ShowPidCommandTest, PrintsSmashProcessIdAndIgnoresArguments) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    testing::internal::CaptureStdout();
    runCommand<ShowPidCommand>("showpid", {"ignored"}, wenv, services);
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ("smash pid is " + std::to_string(getpid()) + "\n", output);
}

TEST(SysInfoCommandTest, PrintsAllRequiredSystemFieldsInOrder) {
#ifndef __linux__
    GTEST_SKIP() << "sysinfo command reads Linux /proc files";
#else
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]() {
        runCommand<SysInfoCommand>("sysinfo", {"ignored"}, wenv, services);
    });
    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    const std::string output = result.stdoutOutput;

    const std::size_t system = output.find("System: ");
    const std::size_t hostname = output.find("Hostname: ");
    const std::size_t kernel = output.find("Kernel: ");
    const std::size_t architecture = output.find("Architecture: ");
    const std::size_t bootTime = output.find("Boot Time: ");

    EXPECT_NE(std::string::npos, system);
    EXPECT_NE(std::string::npos, hostname);
    EXPECT_NE(std::string::npos, kernel);
    EXPECT_NE(std::string::npos, architecture);
    EXPECT_NE(std::string::npos, bootTime);
    EXPECT_LT(system, hostname);
    EXPECT_LT(hostname, kernel);
    EXPECT_LT(kernel, architecture);
    EXPECT_LT(architecture, bootTime);
#endif
}

TEST(WhoAmICommandTest, PrintsUserUidGidAndHomeDirectoryInOrder) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    const uid_t uid = getuid();
    struct passwd* passwd = getpwuid(uid);
    ASSERT_NE(nullptr, passwd);

    testing::internal::CaptureStdout();
    runCommand<WhoAmICommand>("whoami", {"ignored"}, wenv, services);
    const std::string output = testing::internal::GetCapturedStdout();

    std::ostringstream expected;
    expected << passwd->pw_name << "\n"
             << uid << "\n"
             << passwd->pw_gid << "\n"
             << passwd->pw_dir << "\n";

    EXPECT_EQ(expected.str(), output);
}
