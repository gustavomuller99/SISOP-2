#ifndef _HOST_H
#define _HOST_H

#include <pthread.h>
#include <message.h>
#include <iostream>
#include <ncurses.h>

enum HostState {
    Discovery = 1,
    Asleep = 2,
    Awaken = 3,
    Exit = 4
};

struct ManagerInfo {
    std::string ip = "", mac = "", name = "";
};

class Host {
public:
    Host() = default;
    void init();

    void exit_handler(int sn, siginfo_t* t, void* ctx);
    
private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);
    static void* interface(void *ctx);
    static void* input(void *ctx);
    static void* election(void *ctx);

    bool manager_out;

    void switch_state(HostState new_state);
    void create_monitoring_socket();

    int state = HostState::Discovery;
    int prev_state = HostState::Exit;
    
    int sck_discovery;
    int sck_monitoring;

    ManagerInfo m_info = {"-1", "-1", "-1"};

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_interface{};
    pthread_t t_input{};
    pthread_t t_election{};

    pthread_mutex_t mutex_change_state = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_ncurses = PTHREAD_MUTEX_INITIALIZER;

    const int sleep_discovery = 500 * 1000; /* 500 ms */
    const int sleep_output = 500 * 1000;
    const int sleep_input = 25 * 1000;
    const int input_timeout = 25; /* 15 ms */
    const int tcp_timeout = 2; /* 2 s */
    const int election_timeout = 500 * 1000;
};

HostState state_from_string(std::string state);
std::string string_from_state(int state);
WINDOW *create_newwin(int height, int width, int starty, int startx);
void destroy_win(WINDOW *local_win);

#endif //_HOST_H
