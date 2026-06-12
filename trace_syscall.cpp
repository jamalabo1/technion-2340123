#include <unistd.h>

/**
int main() {
    int pipefd[2];

    pipe(pipefd);

    return 0;
}
*/

#include <csignal>

void handler(int /*signum*/) {}

int main() {
    signal(SIGUSR1, handler);

    return 0;
}