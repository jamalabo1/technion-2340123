//
// Created by jamal on 22/05/2026.
//
#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "test_support.h"
#include "built_in.h"
#include "special.h"
#include "exceptions.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

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


    struct linux_dirent64_for_test
    {
        unsigned long long d_ino;
        long long d_off;
        unsigned short d_reclen;
        unsigned char d_type;
        char d_name[];
    };

    long long allocatedBytesNoFollowRecursive(const std::string& path)
    {
        struct stat st{};
        if (lstat(path.c_str(), &st) == -1)
        {
            throw std::runtime_error(
                "lstat failed for " + path + ": " + std::strerror(errno)
            );
        }

        // st_blocks is in 512-byte units. This matches real disk usage semantics.
        long long totalBytes = static_cast<long long>(st.st_blocks) * 512LL;

        // lstat() does not follow symlinks. Therefore symlink targets are not traversed.
        if (!S_ISDIR(st.st_mode))
        {
            return totalBytes;
        }

        int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
        if (fd == -1)
        {
            throw std::runtime_error(
                "open directory failed for " + path + ": " + std::strerror(errno)
            );
        }

        char buffer[32768];

        while (true)
        {
            int bytesRead = static_cast<int>(
                syscall(SYS_getdents64, fd, buffer, sizeof(buffer))
            );

            if (bytesRead == -1)
            {
                int savedErrno = errno;
                close(fd);
                throw std::runtime_error(
                    std::string("getdents64 failed: ") + std::strerror(savedErrno)
                );
            }

            if (bytesRead == 0)
            {
                break;
            }

            for (int bpos = 0; bpos < bytesRead;)
            {
                auto* entry = reinterpret_cast<linux_dirent64_for_test*>(buffer + bpos);
                std::string name(entry->d_name);

                if (name != "." && name != "..")
                {
                    totalBytes += allocatedBytesNoFollowRecursive(path + "/" + name);
                }

                bpos += entry->d_reclen;
            }
        }

        close(fd);
        return totalBytes;
    }

    long long expectedDuKb(const std::string& path)
    {
        const long long bytes = allocatedBytesNoFollowRecursive(path);
        return (bytes + 1023LL) / 1024LL;
    }

    std::string expectedDuOutput(const std::string& path)
    {
        return "Total disk usage: " + std::to_string(expectedDuKb(path)) + " KB\n";
    }

    test_support::ChildRunResult runDuCaptured(const std::vector<std::string>& args)
    {
        return test_support::runInChildCapturingStdout([&]()
        {
            auto services = test_support::makeServices();
            auto wenv = test_support::makeWenv();
            runDu(wenv, services, args);
        });
    }

    void expectCleanDuRun(const test_support::ChildRunResult& result)
    {
        ASSERT_TRUE(WIFEXITED(result.status));
        ASSERT_EQ(0, WEXITSTATUS(result.status));
    }


#endif
} // namespace

// Note: Replace `runDu(...)` with your actual command execution routine.

TEST(DiskUsageCommandTest, UsesCurrentDirectoryWhenNoPathIsProvided)
{
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    // Calculate baseline metadata size of the empty directory
    struct stat st;
    ASSERT_EQ(0, stat(temp.root().c_str(), &st));
    size_t expected_kb = st.st_blocks / 2;

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([]()
    {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {});
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("Total disk usage: " + std::to_string(expected_kb) + " KB\n", result.stdoutOutput);
}

TEST(DiskUsageCommandTest, CalculatesUsageWithFiles)
{
    test_support::TempDir temp;
    std::string filePath = temp.writeFile("data.txt", "12345");

    struct stat dir_st, file_st;
    ASSERT_EQ(0, stat(temp.root().c_str(), &dir_st));
    ASSERT_EQ(0, stat(filePath.c_str(), &file_st));

    size_t expected_kb = (dir_st.st_blocks + file_st.st_blocks) / 2;

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]()
    {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {temp.root()});
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("Total disk usage: " + std::to_string(expected_kb) + " KB\n", result.stdoutOutput);
}

TEST(DiskUsageCommandTest, CalculatesUsageRecursively)
{
    test_support::TempDir temp;
    std::string rootFile = temp.writeFile("root_file.txt", "x");
    std::string subDir = temp.makeSubdir("subdir");

    // Write file directly into the subdirectory
    std::string subFilePath = subDir + "/sub_file.txt";
    std::ofstream out(subFilePath);
    out << "y";
    out.close();

    struct stat dir_st, root_file_st, sub_dir_st, sub_file_st;
    ASSERT_EQ(0, stat(temp.root().c_str(), &dir_st));
    ASSERT_EQ(0, stat(rootFile.c_str(), &root_file_st));
    ASSERT_EQ(0, stat(subDir.c_str(), &sub_dir_st));
    ASSERT_EQ(0, stat(subFilePath.c_str(), &sub_file_st));

    size_t expected_kb = (dir_st.st_blocks + root_file_st.st_blocks +
        sub_dir_st.st_blocks + sub_file_st.st_blocks) / 2;

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]()
    {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {temp.root()});
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("Total disk usage: " + std::to_string(expected_kb) + " KB\n", result.stdoutOutput);
}

TEST(DiskUsageCommandTest, DoesNotFollowSymlinks)
{
    // Create an external directory with a massive file
    test_support::TempDir externalDir;
    std::string massiveFile = externalDir.writeFile("massive.bin", std::string(1024 * 1024, '0')); // 1MB

    // Create the test directory and a symlink pointing to the external directory
    test_support::TempDir testDir;
    std::string symlinkPath = testDir.makeSymlink(externalDir.root(), "external_link");

    // We only sum the test directory metadata and the symlink inode itself, ignoring the target
    struct stat test_dir_st, symlink_st;
    ASSERT_EQ(0, stat(testDir.root().c_str(), &test_dir_st));
    ASSERT_EQ(0, lstat(symlinkPath.c_str(), &symlink_st)); // Must use lstat to avoid following the link

    size_t expected_kb = (test_dir_st.st_blocks + symlink_st.st_blocks) / 2;

    const test_support::ChildRunResult result = test_support::runInChildCapturingStdout([&]()
    {
        auto services = test_support::makeServices();
        auto wenv = test_support::makeWenv();
        runDu(wenv, services, {testDir.root()});
    });

    ASSERT_TRUE(WIFEXITED(result.status));
    ASSERT_EQ(0, WEXITSTATUS(result.status));
    EXPECT_EQ("Total disk usage: " + std::to_string(expected_kb) + " KB\n", result.stdoutOutput);
}

TEST(DiskUsageCommandTest, UsesCurrentDirectoryWhenNoPathIsProvided2)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;

    temp.writeFile("one_byte.txt", "x");
    temp.writeFile("another_file.txt", std::string(3000, 'a'));

    ASSERT_EQ(0, chdir(temp.root().c_str()));

    const auto result = runDuCaptured({});

    expectCleanDuRun(result);
    EXPECT_EQ(expectedDuOutput(temp.root()), result.stdoutOutput);
#endif
}

TEST(DiskUsageCommandTest, ExplicitDirectoryCountsFilesAndSubdirectoriesRecursively)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;

    temp.writeFile("root_file.txt", std::string(17, 'r'));
    temp.makeSubdir("a");
    temp.writeFile("a/file_a.txt", std::string(2049, 'a'));
    temp.makeSubdir("a/b");
    temp.writeFile("a/b/file_b.txt", std::string(5000, 'b'));

    const auto result = runDuCaptured({temp.root()});

    expectCleanDuRun(result);
    EXPECT_EQ(expectedDuOutput(temp.root()), result.stdoutOutput);
#endif
}

TEST(DiskUsageCommandTest, EmptyDirectoryStillPrintsCorrectUsage)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;
    const std::string empty = temp.makeSubdir("empty");

    const auto result = runDuCaptured({empty});

    expectCleanDuRun(result);
    EXPECT_EQ(expectedDuOutput(empty), result.stdoutOutput);
#endif
}

TEST(DiskUsageCommandTest, RelativeDirectoryPathWorks)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;
    test_support::ScopedCwd cwdGuard;

    const std::string child = temp.makeSubdir("child");
    temp.writeFile("child/file.txt", std::string(1234, 'x'));

    ASSERT_EQ(0, chdir(temp.root().c_str()));

    const auto result = runDuCaptured({"child"});

    expectCleanDuRun(result);
    EXPECT_EQ(expectedDuOutput(child), result.stdoutOutput);
#endif
}

TEST(DiskUsageCommandTest, DoesNotFollowSymlinksDuringTraversal)
{
#ifndef __linux__
    GTEST_SKIP() << "du command uses Linux getdents64";
#else
    test_support::TempDir temp;

    const std::string walked = temp.makeSubdir("walked");
    temp.writeFile("walked/local.txt", "local");

    temp.makeSubdir("outside");
    temp.writeFile("outside/large_external_file.bin", std::string(128 * 1024, 'z'));

    temp.makeSymlink(temp.path("outside"), "walked/link_to_outside");

    const auto result = runDuCaptured({walked});

    expectCleanDuRun(result);
    EXPECT_EQ(expectedDuOutput(walked), result.stdoutOutput);
#endif
}