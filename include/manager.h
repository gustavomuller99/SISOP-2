#ifndef _MANAGER_H
#define _MANAGER_H

#include <pthread.h>
#include <message.h>
#include <iostream>
#include <host.h>

struct KnownHost {
    std::string ip;
    std::string name;
    HostState state;
};

class Manager {
public:
    void init();

    std::string state_string(KnownHost host);
    void add_host(KnownHost host);
    bool has_host(std::string name);
    void print_hosts();
    
private:
    static void* discovery(void *ctx);
    static void* monitoring(void *ctx);
    static void* interface(void *ctx);

    std::vector<KnownHost> hosts; // list of known hosts

    int sck_discovery;
    int sck_monitoring;

    pthread_t t_discovery{};
    pthread_t t_monitoring{};
    pthread_t t_interface{};
};

#endif //_MANAGER_H
