#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/capability.h>

asmlinkage long sys_hello(void)
{
    printk("Hello, World2!\n");
    return 0;
}

#define GETPID_FLAG 3
#define PIPE_FLAG 2
#define KILL_FLAG 1
#define SIG_FLAG 0

asmlinkage long sys_set_ban(int ban_getpid, int ban_pipe, int ban_kill, int ban_sig)
{
    if (ban_getpid < 0 || ban_pipe < 0 || ban_kill < 0 || ban_sig < 0)
    {
        return -EINVAL;
    }

    // if not root then return error
    // https://man7.org/linux/man-pages/man7/capabilities.7.html
    if (!capable(CAP_SYS_ADMIN))
    {
        return -EPERM;
    }

    // the ban flag format is
    // 0000[pid][pipe][kill][sig]

    // if 0 will be 0, if 1 will be shifted to (0000)1000
    // double negative like js will turn into "truth"
    char pid_mask = (!!ban_getpid) << GETPID_FLAG;
    char pipe_mask = (!!ban_pipe) << PIPE_FLAG;
    char ban_mask = (!!ban_kill) << KILL_FLAG;
    char sig_mask = (!!ban_sig) << SIG_FLAG;

    // now that i have each mask indicator i'll combine them (or)
    current->ban_flag = pid_mask | pipe_mask | ban_mask | sig_mask;

    return 0;
}

int _internal_get_flag(char ban)
{
    switch (ban)
    {
    case 'g':
        return GETPID_FLAG;
    case 'p':
        return PIPE_FLAG;
    case 'k':
        return KILL_FLAG;
    case 's':
        return SIG_FLAG;
    default:
        return -EINVAL;
    }
}

long _internal_check_ban(const struct task_struct *task, char flag)
{
    // this will put 1 in the flag position
    char flag_mask = (1) << flag;

    // we'll perform and to get the value of that flag only
    char result = task->ban_flag & flag_mask;

    // then I could either double !! or just shift back
    return result >> flag;
}

asmlinkage long sys_get_ban(char ban)
{
    int flag = _internal_get_flag(ban);
    // if <0 then invalid
    if (flag < 0)
        return -EINVAL;

    // check the ban for the current process
    return _internal_check_ban(current, flag);
}

asmlinkage long sys_check_ban(pid_t pid, char ban)
{
    // convert the flag
    int flag = _internal_get_flag(ban);
    // if <0 then invalid
    if (flag < 0)
        return -EINVAL;

    // then get the process task
    struct task_struct *task = pid_task(find_vpid(pid), PIDTYPE_PID);

    if (task == NULL)
    {
        return -ESRCH;
    }

    // then validate if the current process is not banned from this
    if (_internal_check_ban(current, flag))
    {
        return -EPERM;
    };

    // then get the result
    return _internal_check_ban(task, flag);
}

asmlinkage long sys_flip_ban_branch(int height, char ban)
{
    if (height <= 0)
    {
        return -EINVAL;
    }

    int flag = _internal_get_flag(ban);
    if (flag < 0)
    {
        return -EINVAL;
    }

    // then validate if the current process is not banned from this
    if (_internal_check_ban(current, flag))
    {
        return -EPERM;
    };

    int count = 0;

    struct task_struct *task = current->parent;
    // loop until one is reached (null or 0)
    while (task != NULL && height > 0)
    {
        // flip the ban flag using xor
        task->ban_flag ^= (1 << flag);

        // increase count, if was banned then the new value is 1, so we shift to 1 with and
        count += (task->ban_flag >> flag) & 1;

        if(task == task->parent) {
            // we reached root
            break;
        }

        // then go parent
        task = task->parent;
        // and decrease height
        height--;
    }

    // or I could've used "original" height and current height to calculate count, but it's the same (additional var anyway)
    return count;
}