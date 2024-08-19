#ifndef _MANAGER_H
#define _MANAGER_H

#include <pthread.h>
#include <message.h>
#include <iostream>
#include <host.h>

class Manager {
public:
    void init();

    bool b_should_switch_host = false;
    bool b_should_exit_election = false;
    bool b_should_exit = false;
    bool b_should_try_election = false;
    bool b_running_election = false;
    bool b_election_answer = false;

    int election_id = 0;
    int failed_count = 0;

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
    static void* command(void *ctx);
    static void* interface(void *ctx);
    static void* input(void *ctx);
    static void* update_rm(void *ctx);
    static void* listen_election(void *ctx);
    static void* run_election(void *ctx);

    std::pair<int, std::string> check_input(std::string input);
    void send_wake_on_lan_packet(std::string mac_address);

    void update_election_answer(bool value);
    void update_running_election(bool value);

    std::vector<KnownHost> hosts; // list of known hosts
    std::deque<std::pair<int, std::string>> cmd;

    int sck_discovery;
    int sck_management;
    int sck_listen;
    int sck_election;

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_command{};
    pthread_t t_listen_election{};
    pthread_t t_run_election{};
    pthread_t t_interface{};
    pthread_t t_input{};
    pthread_t t_update_rm{};
 
    pthread_mutex_t hosts_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_ncurses = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_answer = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_running_election = PTHREAD_MUTEX_INITIALIZER;

    const int sleep_monitoring = 500 * 1000; /* 500 ms */
    const int sleep_command = 500 * 1000;
    const int sleep_output = 500 * 1000;
    const int sleep_update = 500 * 1000;
    const int sleep_run_election = 500 * 1000;
    const int sleep_answer = 5000 * 1000; 
    const int sleep_input = 25 * 1000;
    const int input_timeout = 25; /* 25 ms */
    const int tcp_timeout = 250 * 1000;
};

#endif //_MANAGER_H
