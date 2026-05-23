#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "built_in.h"
#include "exceptions.h"
#include "services.h"

namespace
{
    std::string getCwd()
    {
        char* raw = getcwd(nullptr, 0);
        if (raw == nullptr)
        {
            throw std::runtime_error("getcwd failed");
        }

        std::string cwd(raw);
        free(raw);
        return cwd;
    }

    class TempDir
    {
    public:
        TempDir()
        {
            char path_template[] = "/tmp/smash_cd_test_XXXXXX";
            char* created = mkdtemp(path_template);
            if (created == nullptr)
            {
                throw std::runtime_error("mkdtemp failed");
            }
            root_ = created;
        }

        ~TempDir()
        {
            for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            {
                rmdir(it->c_str());
            }
            rmdir(root_.c_str());
        }

        const std::string& root() const
        {
            return root_;
        }

        std::string makeSubdir(const std::string& name)
        {
            std::string path = root_ + "/" + name;
            if (mkdir(path.c_str(), 0700) == -1)
            {
                throw std::runtime_error("mkdir failed");
            }
            children_.push_back(path);
            return path;
        }

    private:
        std::string root_;
        std::vector<std::string> children_;
    };

    class ScopedCwd
    {
    public:
        ScopedCwd() : original_(getCwd())
        {
        }

        ~ScopedCwd()
        {
            chdir(original_.c_str());
        }

    private:
        std::string original_;
    };

    std::shared_ptr<Services> makeServices()
    {
        return std::make_shared<Services>(
            std::make_shared<JobTable>(),
            std::make_shared<CommandProcManagement>(),
            std::make_shared<Aliases>(),
            std::make_shared<ChildTracker>()
        );
    }

    std::shared_ptr<WorkingEnvironment> makePopulatedWenv()
    {
        auto wenv = std::make_shared<WorkingEnvironment>("smash");
        wenv->pushDir(getCwd());
        return wenv;
    }

    void runCd(
        const std::shared_ptr<WorkingEnvironment>& wenv,
        const std::shared_ptr<Services>& services,
        const std::vector<std::string>& args
    )
    {
        Parameters params(args);
        CommandCtx::CommandFlags flags;
        flags.isBackground = false;

        CommandCtx ctx("cd", wenv, params, flags, services);
        ChangeDirCommand command(ctx);
        command.run();
    }
} // namespace

TEST(ChangeDirCommandTest, NoArgumentsDoesNotChangeDirectory)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto wenv = makePopulatedWenv();
    auto services = makeServices();

    runCd(wenv, services, {});

    EXPECT_EQ(temp.root(), getCwd());
}

TEST(ChangeDirCommandTest, ChangesToRequestedDirectory)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    const std::string target = temp.makeSubdir("target");
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto wenv = makePopulatedWenv();
    auto services = makeServices();

    runCd(wenv, services, {target});

    EXPECT_EQ(target, getCwd());
}

TEST(ChangeDirCommandTest, StoresResolvedDirectoryWhenChangingWithRelativePath)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    const std::string target = temp.makeSubdir("target");
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto wenv = makePopulatedWenv();
    auto services = makeServices();

    runCd(wenv, services, {"target"});

    EXPECT_EQ(target, getCwd());
    EXPECT_EQ(target, wenv->getCurrentWorkingDir());
    EXPECT_EQ(temp.root(), wenv->getLastWorkingDir());

    runCd(wenv, services, {"-"});

    EXPECT_EQ(temp.root(), getCwd());
    EXPECT_EQ(temp.root(), wenv->getCurrentWorkingDir());
    EXPECT_EQ(target, wenv->getLastWorkingDir());
}

TEST(ChangeDirCommandTest, RejectsTooManyArgumentsWithoutChangingDirectory)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    const std::string first = temp.makeSubdir("first");
    const std::string second = temp.makeSubdir("second");
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto wenv = makePopulatedWenv();
    auto services = makeServices();

    EXPECT_THROW(runCd(wenv, services, {first, second}), CommandTooManyArguments);
    EXPECT_EQ(temp.root(), getCwd());
}

TEST(ChangeDirCommandTest, DashWithoutPreviousDirectoryReportsOldpwdNotSet)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    int chdirResult = chdir(temp.root().c_str());
    ASSERT_EQ(0, chdirResult);

    auto wenv = std::make_shared<WorkingEnvironment>("smash");
    auto services = makeServices();

    try
    {
        runCd(wenv, services, {"-"});
        FAIL() << "expected cd - to fail before any successful cd";
    }
    catch (const CommandDomainError& e)
    {
        EXPECT_STREQ("OLDPWD not set", e.what());
    }

    EXPECT_EQ(temp.root(), getCwd());
}

TEST(ChangeDirCommandTest, DashReturnsToPreviousDirectoryAfterOneSuccessfulCd)
{
    TempDir temp;
    ScopedCwd cwd_guard;
    const std::string target = temp.makeSubdir("target");
    ASSERT_EQ(0, chdir(temp.root().c_str()));

    auto wenv = makePopulatedWenv();
    auto services = makeServices();

    runCd(wenv, services, {target});
    ASSERT_EQ(target, getCwd());

    runCd(wenv, services, {"-"});

    EXPECT_EQ(temp.root(), getCwd());
}
