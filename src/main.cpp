#include <host.h>
#include <manager.h>
#include <memory>

bool is_manager;
std::unique_ptr<Manager> m;
std::unique_ptr<Host> h;

void exit_handler(int sn, siginfo_t* t, void* ctx) {
    if (is_manager) {
        exit(0);
    } else {
        h->exit_handler(sn, t, ctx);
    }
}

int main(int argc, char *argv[]) {

    struct sigaction sig_int_handler;
    sig_int_handler.sa_sigaction = exit_handler;
    sigaction(SIGINT, &sig_int_handler, NULL);

    if (argc > 1 && strcmp(argv[1], "manager") == 0) {
        is_manager = true;
        m = std::make_unique<Manager>(Manager());
        m->init();
    } else {
        is_manager = false;
        h = std::make_unique<Host>(Host());
        h->init();
    }
}

