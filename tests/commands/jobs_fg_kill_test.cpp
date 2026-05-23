#include <gtest/gtest.h>

#include <signal.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include "test_support.h"
#include "built_in.h"
#include "exceptions.h"

namespace {

void runKill(
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services,
    const std::vector<std::string>& args
) {
    CommandCtx ctx = test_support::makeCtx("kill", wenv, services, args);
    KillCommand command(ctx);
    command.run();
}

void runFg(
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services,
    const std::vector<std::string>& args
) {
    CommandCtx ctx = test_support::makeCtx("fg", wenv, services, args);
    ForegroundCommand command(ctx);
    command.run();
}

} // namespace

TEST(KillCommandTest, RejectsMalformedArguments) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    EXPECT_THROW(runKill(wenv, services, {"9", "1"}), CommandInvalidArguments);
    EXPECT_THROW(runKill(wenv, services, {"-abc", "1"}), CommandInvalidArguments);
    EXPECT_THROW(runKill(wenv, services, {"-9"}), CommandInvalidArguments);
    EXPECT_THROW(runKill(wenv, services, {"-9", "1", "extra"}), CommandInvalidArguments);
    EXPECT_THROW(runKill(wenv, services, {"-9", "0"}), CommandInvalidArguments);
}

TEST(KillCommandTest, ReportsMissingJobBeforeSendingSignal) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    try {
        runKill(wenv, services, {"-9", "7"});
        FAIL() << "expected missing job to fail";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("job-id 7 does not exist", e.what());
    }
}

TEST(KillCommandTest, SendsSignalAndPrintsTargetPid) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    auto child = test_support::forkSleepingChild();
    services->table->addJob("sleep 30&", test_support::makeProc(child.pid()));

    testing::internal::CaptureStdout();
    runKill(wenv, services, {"-15", "1"});
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ("signal number 15 was sent to pid " + std::to_string(child.pid()) + "\n", output);

    const int status = test_support::waitFor(child.pid());
    child.markReaped();
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(SIGTERM, WTERMSIG(status));
}

TEST(ForegroundCommandTest, RejectsMalformedArguments) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    EXPECT_THROW(runFg(wenv, services, {"abc"}), CommandInvalidArguments);
    EXPECT_THROW(runFg(wenv, services, {"1", "extra"}), CommandInvalidArguments);
    EXPECT_THROW(runFg(wenv, services, {"0"}), CommandInvalidArguments);
}

TEST(ForegroundCommandTest, ReportsEmptyJobsListWithoutJobId) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    try {
        runFg(wenv, services, {});
        FAIL() << "expected empty jobs list to fail";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("jobs list is empty", e.what());
    }
}

TEST(ForegroundCommandTest, ReportsMissingExplicitJobId) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    try {
        runFg(wenv, services, {"4"});
        FAIL() << "expected missing job to fail";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("job-id 4 does not exist", e.what());
    }
}

TEST(ForegroundCommandTest, BringsMaxJobToForegroundWhenNoIdProvided) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    auto older = test_support::forkSleepingChild();
    auto newest = test_support::forkExitingChild();

    services->table->addJob("sleep 30&", test_support::makeProc(older.pid()));
    services->table->addJob("true&", test_support::makeProc(newest.pid()));

    testing::internal::CaptureStdout();
    runFg(wenv, services, {});
    const std::string output = testing::internal::GetCapturedStdout();
    newest.markReaped();

    EXPECT_EQ("true& " + std::to_string(newest.pid()) + "\n", output);
    EXPECT_NE(nullptr, services->table->get_jobById(1));
    EXPECT_EQ(nullptr, services->table->get_jobById(2));
}

TEST(ForegroundCommandTest, BringsExplicitJobToForegroundAndRemovesIt) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    auto first = test_support::forkExitingChild();
    auto second = test_support::forkSleepingChild();

    services->table->addJob("true&", test_support::makeProc(first.pid()));
    services->table->addJob("sleep 30&", test_support::makeProc(second.pid()));

    testing::internal::CaptureStdout();
    runFg(wenv, services, {"1"});
    const std::string output = testing::internal::GetCapturedStdout();
    first.markReaped();

    EXPECT_EQ("true& " + std::to_string(first.pid()) + "\n", output);
    EXPECT_EQ(nullptr, services->table->get_jobById(1));
    EXPECT_NE(nullptr, services->table->get_jobById(2));
}
