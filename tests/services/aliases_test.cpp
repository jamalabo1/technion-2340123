#include <gtest/gtest.h>

#include <memory>

#include "command_factory.h"
#include "built_in.h"
#include "aliases.h"

TEST(AliasesServiceTest, AddsTokenizesAndRemovesAliases) {
    Aliases aliases;

    ASSERT_TRUE(aliases.addAlias("ll", "ls -l"));
    EXPECT_EQ("ls -l", aliases.tokenize("ll"));
    EXPECT_EQ("pwd", aliases.tokenize("pwd"));

    EXPECT_FALSE(aliases.addAlias("ll", "echo duplicate"));
    EXPECT_TRUE(aliases.removeAlias("ll"));
    EXPECT_FALSE(aliases.removeAlias("ll"));
    EXPECT_EQ("ll", aliases.tokenize("ll"));
}

TEST(AliasesServiceTest, RejectsReservedCommandsAsAliasNames) {
    CommandCtx::CommandFlags flags;
    flags.isBackground = false;
    auto factory = CommandFactory::getInstance();
    auto services = std::make_shared<Services>(
        std::make_shared<JobTable>(),
        std::make_shared<CommandProcManagement>(),
        std::make_shared<Aliases>(),
        std::make_shared<ChildTracker>()
    );
    auto wenv = std::make_shared<WorkingEnvironment>("smash");
    CommandCtx ctx("pwd", wenv, Parameters({}), flags, services);
    GetCurrDirCommand forceRegistration(ctx);

    Aliases aliases;

    EXPECT_FALSE(aliases.addAlias("pwd", "echo should-not-shadow-built-in"));
}
