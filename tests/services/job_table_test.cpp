#include <gtest/gtest.h>

#include <memory>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "../test_support.h"
#include "services/job_table.h"

namespace
{
    std::shared_ptr<CommandProc> makeProc(int pid)
    {
        auto proc = std::make_shared<CommandProc>();
        proc->pId = pid;
        return proc;
    }
} // namespace

TEST(JobTableTest, AddsJobsWithOneBasedIdsAndAllowsLookup)
{
    JobTable table;
    auto first = makeProc(111);
    auto second = makeProc(222);

    table.addJob("sleep 1&", first);
    table.addJob("sleep 2&", second);

    EXPECT_EQ(1, first->assignedJobId);
    EXPECT_EQ(2, second->assignedJobId);

    auto first_job = table.get_jobById(1);
    auto second_job = table.get_jobById(2);

    ASSERT_NE(nullptr, first_job);
    ASSERT_NE(nullptr, second_job);
    EXPECT_EQ(111, first_job->get_pId());
    EXPECT_EQ(222, second_job->get_pId());
    EXPECT_EQ(second_job, table.get_maxIdJob());
}

TEST(JobTableTest, PrintsJobsInAscendingJobIdOrder)
{
    JobTable table;

    auto job1 = test_support::forkSleepingChild();
    auto job2 = test_support::forkSleepingChild();
    auto job3 = test_support::forkSleepingChild();

    table.addJob("sleep 1&", makeProc(job1.pid()));
    table.addJob("sleep 2&", makeProc(job2.pid()));
    table.addJob("sleep 3&", makeProc(job3.pid()));

    testing::internal::CaptureStdout();
    table.printJobsList();
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ("[1] sleep 1&\n[2] sleep 2&\n[3] sleep 3&\n", output);
}

//
TEST(JobTableTest, ReusesMaxPlusOneAfterRemovingMiddleJob)
{
    JobTable table;

    auto job1 = test_support::forkSleepingChild();
    auto job2 = test_support::forkSleepingChild();
    auto job3 = test_support::forkSleepingChild();


    table.addJob("sleep 1&", makeProc(job1.pid()));
    table.addJob("sleep 2&", makeProc(job2.pid()));
    table.addJob("sleep 3&", makeProc(job3.pid()));

    table.removeJobById(2);

    auto job4 = test_support::forkSleepingChild();
    auto next = makeProc(job4.pid());

    table.addJob("sleep 4&", next);

    EXPECT_EQ(4, next->assignedJobId);
    EXPECT_EQ(nullptr, table.get_jobById(2));
    ASSERT_NE(nullptr, table.get_jobById(4));
    EXPECT_EQ(job4.pid(), table.get_jobById(4)->get_pId());
}

TEST(JobTableTest, ResetsIdsToOneAfterAllJobsAreRemoved)
{
    JobTable table;

    auto job1 = test_support::forkSleepingChild();
    auto job2 = test_support::forkSleepingChild();


    table.addJob("sleep 1&", makeProc(job1.pid()));
    table.addJob("sleep 2&", makeProc(job2.pid()));

    table.removeJobById(1);
    table.removeJobById(2);

    auto job3 = test_support::forkSleepingChild();

    auto next = makeProc(job3.pid());
    table.addJob("sleep 3&", next);

    EXPECT_EQ(1, next->assignedJobId);
    ASSERT_NE(nullptr, table.get_jobById(1));
    EXPECT_EQ(job3.pid(), table.get_jobById(1)->get_pId());
}


TEST(JobTableTest, EmptyTableHasNoMaxOrMatchingJob)
{
    JobTable table;

    EXPECT_EQ(nullptr, table.get_maxIdJob());
    EXPECT_EQ(nullptr, table.get_jobById(99));
}

TEST(JobTableTest, RemoveFinishedJobsKeepsRunningJobsOnly)
{
    JobTable table;
    auto finished = test_support::forkExitingChild();
    auto running = test_support::forkSleepingChild();

    table.addJob("true&", makeProc(finished.pid()));
    table.addJob("sleep 30&", makeProc(running.pid()));

    usleep(50000);
    table.removeFinishedJobs();
    finished.markReaped();

    EXPECT_EQ(nullptr, table.get_jobById(1));
    ASSERT_NE(nullptr, table.get_jobById(2));
    EXPECT_EQ(running.pid(), table.get_jobById(2)->get_pId());
}

TEST(JobTableTest, KillAndRemoveAllJobsPrintsPidCommandPairsAndClearsTable)
{
    JobTable table;
    auto first = test_support::forkSleepingChild();
    auto second = test_support::forkSleepingChild();

    table.addJob("sleep 30&", makeProc(first.pid()));
    table.addJob("sleep 40&", makeProc(second.pid()));

    testing::internal::CaptureStdout();
    table.killAndRemoveAllJobs();
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_EQ(
        "sending SIGKILL signal to 2 jobs:\n" +
        std::to_string(first.pid()) + ": sleep 30&\n" +
        std::to_string(second.pid()) + ": sleep 40&\n",
        output
    );
    EXPECT_EQ(nullptr, table.get_maxIdJob());

    int firstStatus = test_support::waitFor(first.pid());
    int secondStatus = test_support::waitFor(second.pid());
    first.markReaped();
    second.markReaped();

    EXPECT_TRUE(WIFSIGNALED(firstStatus));
    EXPECT_TRUE(WIFSIGNALED(secondStatus));
    EXPECT_EQ(SIGKILL, WTERMSIG(firstStatus));
    EXPECT_EQ(SIGKILL, WTERMSIG(secondStatus));
}
