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

class Host {
public:
    Host() = default;
    void init();
    
private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);
    static void* interface(void *ctx);
    static void* input(void *ctx);

    void switch_state(HostState new_state);

    int state = HostState::Discovery;
    int prev_state = HostState::Exit;
    
    int sck_discovery;
    int sck_monitoring;

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_interface{};
    pthread_t t_input{};

    pthread_mutex_t mutex_change_state = PTHREAD_MUTEX_INITIALIZER;

    const int sleep_discovery = 500 * 1000; /* 500 ms */
    const int sleep_interface = 500 * 1000;
    const int sleep_monitoring = 250 * 1000;
};

HostState state_from_string(std::string state);
std::string string_from_state(int state);
WINDOW *create_newwin(int height, int width, int starty, int startx);
void destroy_win(WINDOW *local_win);

#endif //_HOST_H
