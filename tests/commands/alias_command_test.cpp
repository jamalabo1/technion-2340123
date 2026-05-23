#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_support.h"
#include "built_in.h"
#include "exceptions.h"

namespace {

void runAlias(
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services,
    const std::vector<std::string>& args
) {
    CommandCtx ctx = test_support::makeCtx("alias", wenv, services, args);
    AliasCommand command(ctx);
    command.run();
}

void runUnAlias(
    const std::shared_ptr<WorkingEnvironment>& wenv,
    const std::shared_ptr<Services>& services,
    const std::vector<std::string>& args
) {
    CommandCtx ctx = test_support::makeCtx("unalias", wenv, services, args);
    UnAliasCommand command(ctx);
    command.run();
}

} // namespace

TEST(AliasCommandTest, CreatesAliasWithSpacesInsideQuotedCommand) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    runAlias(wenv, services, {"ll='ls", "-l'"});

    EXPECT_EQ("ls -l", services->aliases->tokenize("ll"));
}

TEST(AliasCommandTest, ListsAliasesInCreationOrderWithAssignmentSyntax) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    runAlias(wenv, services, {"zeta='echo", "last'"});
    runAlias(wenv, services, {"alpha='echo", "first'"});

    testing::internal::CaptureStdout();
    runAlias(wenv, services, {});
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ("zeta='echo last'\nalpha='echo first'\n", output);
}

TEST(AliasCommandTest, RejectsInvalidAliasFormatsBeforeMutatingState) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    EXPECT_THROW(runAlias(wenv, services, {"bad-name='pwd'"}), CommandDomainError);
    EXPECT_THROW(runAlias(wenv, services, {"missing_quotes=pwd"}), CommandDomainError);
    EXPECT_THROW(runAlias(wenv, services, {"too='many'", "tokens"}), CommandDomainError);

    EXPECT_EQ("bad-name", services->aliases->tokenize("bad-name"));
    EXPECT_EQ("missing_quotes", services->aliases->tokenize("missing_quotes"));
    EXPECT_EQ("too", services->aliases->tokenize("too"));
}

TEST(AliasCommandTest, RejectsDuplicateAndReservedAliasNames) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    runAlias(wenv, services, {"ll='ls'"});

    try {
        runAlias(wenv, services, {"ll='pwd'"});
        FAIL() << "expected duplicate alias to fail";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("ll already exists or is a reserved command", e.what());
    }

    try {
        runAlias(wenv, services, {"pwd='echo", "reserved'"});
        FAIL() << "expected reserved alias to fail";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("pwd already exists or is a reserved command", e.what());
    }
}

TEST(UnAliasCommandTest, RequiresAtLeastOneAliasName) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();

    EXPECT_THROW(runUnAlias(wenv, services, {}), CommandNotEnoughArguments);
}

TEST(UnAliasCommandTest, StopsAtFirstMissingAliasAndKeepsLaterAliases) {
    auto services = test_support::makeServices();
    auto wenv = test_support::makeWenv();
    runAlias(wenv, services, {"first='pwd'"});
    runAlias(wenv, services, {"second='showpid'"});

    try {
        runUnAlias(wenv, services, {"first", "missing", "second"});
        FAIL() << "expected unalias to fail on the first missing alias";
    } catch (const CommandDomainError& e) {
        EXPECT_STREQ("missing alias does not exist", e.what());
    }

    EXPECT_EQ("first", services->aliases->tokenize("first"));
    EXPECT_EQ("showpid", services->aliases->tokenize("second"));
}
