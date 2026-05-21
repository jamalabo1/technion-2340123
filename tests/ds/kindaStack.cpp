#include <gtest/gtest.h>

#include <string>

#include "ds/KindaStack.h"

TEST(KindaStackTest, NewStackHasNoCurrentOrPreviousValue) {
    KindaStack<std::string> stack;

    EXPECT_EQ(0u, stack.size());
    EXPECT_THROW(stack.top(), std::runtime_error);
    EXPECT_THROW(stack.previous(), std::runtime_error);
}

TEST(KindaStackTest, SingleValueIsCurrentAndPreviousIsUnusedSlot) {
    KindaStack<std::string> stack;

    stack.push("first");

    EXPECT_EQ(1u, stack.size());
    EXPECT_EQ("first", stack.top());
    EXPECT_EQ("", stack.previous());
}

TEST(KindaStackTest, TracksCurrentAndPreviousValues) {
    KindaStack<std::string> stack;

    stack.push("first");
    stack.push("second");

    EXPECT_EQ(2u, stack.size());
    EXPECT_EQ("second", stack.top());
    EXPECT_EQ("first", stack.previous());
}

TEST(KindaStackTest, KeepsOnlyTheTwoMostRecentValues) {
    KindaStack<std::string> stack;

    stack.push("first");
    stack.push("second");
    stack.push("third");

    EXPECT_EQ(2u, stack.size());
    EXPECT_EQ("third", stack.top());
    EXPECT_EQ("second", stack.previous());
}


TEST(KindaStackTest, MyUnderstanding) {
    KindaStack<std::string> stack;

    EXPECT_THROW(stack.top(), std::runtime_error);


    stack.push("first");

    // what's the current dir?
    EXPECT_EQ(stack.top(), "first");

    EXPECT_EQ(stack.previous(), "");

    stack.push("second");

    // what's the current dir?
    EXPECT_EQ(stack.top(), "second");

    // ok use previous to loop
    stack.push(
        stack.previous()
    );

    // what's the current dir?
    EXPECT_EQ(stack.top(), "first");

    // ok use previous to loop
    stack.push(
        stack.previous()
    );

    // what's the current dir?
    EXPECT_EQ(stack.top(), "second");

    // introduce a loop breaker
    stack.push("third");

    // what's the current dir?
    EXPECT_EQ(stack.top(), "third");

    // ok use previous to loop
    stack.push(
        stack.previous()
    );

    // what's the current dir?
    EXPECT_EQ(stack.top(), "second");
}
