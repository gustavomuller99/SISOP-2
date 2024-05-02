#include <host.h>

void Host::init() {
    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Host::monitoring, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
}

void *Host::discovery(void *ctx) {
    Host *h = ((Host *) ctx);

    // creating udp socket file descriptor
    int trueflag = 1;

    if ((h->sck_discovery = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        exit(EXIT_FAILURE); 

    if (setsockopt(h->sck_discovery, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE);

    while (h->state == HostState::Discovery) {
        Packet p = Packet(MessageType::SleepServiceDiscovery, 0, 0);
       
        char hostname[BUFFER_SIZE];
        gethostname(hostname, BUFFER_SIZE);
        p.push(hostname);
        
        send_broadcast(p, h->sck_discovery, PORT_DISCOVERY);

        usleep(h->sleep_discovery);
    }

    close(h->sck_discovery);
    return 0;
}

void *Host::monitoring(void *ctx) {
    Host *h = ((Host *) ctx);

    int trueflag = 1;

    if ((h->sck_monitoring = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        exit(EXIT_FAILURE); 

    if (setsockopt(h->sck_monitoring, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE);

    while (h->state == HostState::Asleep || h->state == HostState::Awaken) {
        Packet p = rec_packet(h->sck_monitoring);
        p.print();
    }

    return 0;
}

