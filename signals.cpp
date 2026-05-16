#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"
#include "shell.h"
#include "utils.h"

using namespace std;

void ctrlCHandler(int sig_num)
{
    SmallShell& shell = SmallShell::getInstance();

    shell.forward_signal(sig_num);
}
