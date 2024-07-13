#ifndef _MANAGER_H
#define _MANAGER_H

#include <pthread.h>
#include <message.h>
#include <iostream>
#include <host.h>

struct KnownHost {
    std::string ip;
    std::string mac;
    std::string name;
    HostState state;
    bool connected; // track host socket connected
    int sockfd; // host socket
};

class Manager {
public:
    void init();

    /* safe operations over hosts list */
    void add_host(KnownHost host);
    void remove_host(KnownHost host);
    std::vector<KnownHost> get_hosts(); // get a copy of host list, useful for printing
    /* --- */
    bool has_host(std::string name);
    void exit_handler(int sn, siginfo_t* t, void* ctx);

private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);
    static void* management(void *ctx);
    static void* command(void *ctx);
    static void* interface(void *ctx);
    static void* input(void *ctx);

    std::pair<int, std::string> check_input(std::string input);

    std::vector<KnownHost> hosts; // list of known hosts
    std::deque<std::pair<int, std::string>> cmd;

    int sck_discovery;
    int sck_management;

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_management{};
    pthread_t t_command{};
    pthread_t t_interface{};
    pthread_t t_input{};

    pthread_mutex_t hosts_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_ncurses = PTHREAD_MUTEX_INITIALIZER;

    const int sleep_monitoring = 500 * 1000; /* 500 ms */
    const int sleep_command = 500 * 1000;
    const int sleep_output = 500 * 1000;
    const int sleep_input = 25 * 1000;
    const int input_timeout = 25; /* 10 ms */
};

#endif //_MANAGER_H
