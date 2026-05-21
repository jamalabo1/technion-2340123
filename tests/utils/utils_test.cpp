#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "utils.h"

TEST(UtilsTest, TrimsAllSupportedWhitespaceFromBothSides) {
    EXPECT_EQ("hello world", Shell::Utils::_trim(" \t\nhello world \n\t"));
    EXPECT_EQ("", Shell::Utils::_trim(" \t\n"));
}

TEST(UtilsTest, SplitsOnWhitespaceAndDropsEmptyTokens) {
    const std::vector<std::string> parts = Shell::Utils::_split("  alpha\t beta   gamma  ");

    ASSERT_EQ(3u, parts.size());
    EXPECT_EQ("alpha", parts[0]);
    EXPECT_EQ("beta", parts[1]);
    EXPECT_EQ("gamma", parts[2]);
}

TEST(UtilsTest, SplitsOnRegexDelimiterAndTrimsEachSide) {
    const std::vector<std::string> parts = Shell::Utils::_split(" left  |&  right ", R"(\|&)");

    ASSERT_EQ(2u, parts.size());
    EXPECT_EQ("left", parts[0]);
    EXPECT_EQ("right", parts[1]);
}

TEST(UtilsTest, RemovesOnlyTrailingBackgroundMarker) {
    std::string background = "  sleep 10   &   ";
    background = Shell::Utils::_trim(background);

    EXPECT_TRUE(Shell::Utils::_checkAndRemoveBgCommand(background));
    EXPECT_EQ("sleep 10", background);

    std::string foreground = "echo a&b";
    EXPECT_FALSE(Shell::Utils::_checkAndRemoveBgCommand(foreground));
    EXPECT_EQ("echo a&b", foreground);
}

TEST(UtilsTest, ExtractsCommandAndParametersAfterNormalization) {
    const std::string commandLine = "pwd\tignored   arguments";

    EXPECT_EQ("pwd", Shell::Utils::_command(commandLine));

    const std::vector<std::string> params = Shell::Utils::_params(commandLine);
    ASSERT_EQ(2u, params.size());
    EXPECT_EQ("ignored", params[0]);
    EXPECT_EQ("arguments", params[1]);
}
