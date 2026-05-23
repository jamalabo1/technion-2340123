#include <gtest/gtest.h>

#include <memory>
#include <sys/wait.h>

#include "test_support.h"
#include "proc_mng.h"

TEST(CommandProcManagementTest, SetClearAndGetForegroundProcess) {
    CommandProcManagement management;
    auto proc = std::make_shared<CommandProc>();
    proc->pId = 1234;

    EXPECT_EQ(nullptr, management.getFgProc());

    management.setFgProc(proc);
    EXPECT_EQ(proc, management.getFgProc());

    management.clearFgProc();
    EXPECT_EQ(nullptr, management.getFgProc());
}

TEST(CommandProcManagementTest, WaitingForForegroundProcessClearsIt) {
    CommandProcManagement management;
    auto child = test_support::forkExitingChild();
    auto proc = test_support::makeProc(child.pid());

    management.setAndWaitFgProc(proc);
    child.markReaped();

    EXPECT_EQ(nullptr, management.getFgProc());
}

TEST(CommandProcManagementTest, WaitWithoutForegroundProcessIsLogicError) {
    CommandProcManagement management;

    EXPECT_THROW(management.waitFgProc(), std::logic_error);
}
