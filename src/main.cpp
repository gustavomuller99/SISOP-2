#include <election.h>
#include <memory>

std::unique_ptr<Election> election;

void exit_handler(int sn, siginfo_t* t, void* ctx) {
    election->exit_handler(sn, t, ctx);
}

int main(int argc, char *argv[]) {
    struct sigaction sig_int_handler;
    sig_int_handler.sa_sigaction = exit_handler;
    sigaction(SIGINT, &sig_int_handler, NULL);

    election = std::make_unique<Election>(Election());
    election->init();
}

