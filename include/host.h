#ifndef _HOST_H
#define _HOST_H

#include <pthread.h>
#include <message.h>
#include <iostream>

enum HostState {
    Discovery = 1,
    Asleep = 2,
    Awaken = 3
};

class Host {
public:
    Host() = default;
    void init();
    
private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);

    int state = HostState::Discovery;
    
    int sck_discovery;
    int sck_monitoring;

    pthread_t t_discovery{};
    pthread_t t_monitoring{};

    const int sleep_discovery = 500 * 1000; /* 500 ms */
};

#endif //_HOST_H
