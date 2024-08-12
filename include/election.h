#include <manager.h>
#include <host.h>
#include <memory>
#include <pthread.h>

enum RunningType {
    AsHost = 1,
    AsManager = 2
};

class Election {
public:
    Election() = default;
    void init();
    void exit_handler(int sn, siginfo_t* t, void* ctx);
    void switch_manager();
private:
    int running_as = RunningType::AsHost;
    std::unique_ptr<Host> h;
    std::unique_ptr<Manager> m;

    static void* check(void *ctx);
    static void* host(void *ctx);
    static void* manager(void *ctx);

    pthread_t t_check{};
    pthread_t t_host{};
    pthread_t t_manager{};

    const int sleep_check = 500 * 1000;
};