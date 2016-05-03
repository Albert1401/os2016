#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <zconf.h>

void handler(int sig, siginfo_t *siginfo, void * ign) {
    char *str = sig == SIGUSR1 ? "SIGUSR1" : "SIGUSR2";
    printf("%s from %d\n", str, siginfo->si_pid);
    exit(0);
}

int main(int argc, char **argv) {
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGUSR2);

    struct sigaction action;
    action.sa_sigaction = handler;
    action.sa_flags = SA_SIGINFO;
    action.sa_mask = mask;

    sigprocmask(SIG_SETMASK, &mask, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    sleep(10);
    printf("No signals caught\n");
    return 0;
}
