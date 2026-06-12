# self documentation

## pcb
changes to the pcb struct are done in `include/linux/sched.h`
there I have added `char ban_flag` in the randomized section

to init this flag I have changed `init_task.h` to start with `0`

### reading the pcb
To read the pcb according to `sys_kill` I'll perform:
```c++
    pid_task(pid, PIDTYPE_PID);
```
given a pid. this lives in `<linux/pid.h>`

or according to https://kernelnewbies.org/FAQ/current I can use current macro 


## common.c

I changed the `do_syscall_64` function to first check the syscall using `nr` and jump otherwise
