#ifndef _HOST_H
#define _HOST_H

#include <pthread.h>
#include <message.h>
#include <iostream>
#include <ncurses.h>

struct ManagerInfo {
    std::string ip = "", mac = "", name = "";
};

struct KnownHost {
    std::string ip;
    std::string mac;
    std::string name;
    HostState state;
    bool connected; // track host socket connected
    int sockfd; // host socket
    long election_id;
};

class Host {
public:
    Host() = default;
    void init();

    void exit_handler(int sn, siginfo_t* t, void* ctx);
    std::vector<KnownHost> get_hosts();
    
    int election_id = 0;
    bool manager_up = false;
    bool b_should_switch_manager = false;
    bool b_should_exit_election = false;
    int state = HostState::Discovery;
    int prev_state = HostState::Exit;
    
private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);
    static void* check_manager(void *ctx);
    static void* listen_election(void *ctx);
    static void* run_election(void *ctx);
    static void* interface(void *ctx);
    static void* input(void *ctx);

    void switch_state(HostState new_state);
    void create_monitoring_socket();
    void update_election_answer(bool value);

    
    int sck_discovery;
    int sck_monitoring;
    int sck_listen;
    int sck_election;

    int manager_conn_count = 0;

    bool b_election_answer = false;

    ManagerInfo m_info = {"-1", "-1", "-1"};
    std::vector<KnownHost> hosts_replica; // list of known hosts sent by the manager

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_check_manager{};
    pthread_t t_listen_election{};
    pthread_t t_run_election{};
    pthread_t t_interface{};
    pthread_t t_input{};

    pthread_mutex_t mutex_change_state = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_hosts_replica = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_ncurses = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_answer = PTHREAD_MUTEX_INITIALIZER;

    const int sleep_discovery = 500 * 1000; /* 500 ms */
    const int sleep_monitoring = 50 * 1000;
    const int sleep_check_manager = 5000 * 1000;
    const int sleep_run_election = 500 * 1000;
    const int sleep_answer = 7000 * 1000; 
    const int sleep_output = 500 * 1000;
    const int sleep_input = 25 * 1000;
    const int input_timeout = 25;
    const int tcp_timeout = 5; /* 5 s */
};

WINDOW *create_newwin(int height, int width, int starty, int startx);
void destroy_win(WINDOW *local_win);

#endif //_HOST_H
