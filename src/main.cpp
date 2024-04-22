#include <host.h>
#include <manager.h>
#include <memory>

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "manager") == 0) {
        std::unique_ptr<Manager> m = std::make_unique<Manager>(Manager());
        m->init();
    } else {
        std::unique_ptr<Host> h = std::make_unique<Host>(Host());
        h->init();
    }
}