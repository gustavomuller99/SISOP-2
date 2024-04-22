#include <host.h>

void Host::init() {
    this->init_discovery();
    // this->init_monitoring();
    // this->init_management();
    // this->init_interface();
}

void Host::init_discovery() {
    // creating udp socket file descriptor
    int trueflag = 1;

    if ((this->sck_discovery = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        exit(EXIT_FAILURE); 

    if (setsockopt(this->sck_discovery, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE);

    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_join(this->t_discovery, NULL);

    close(this->sck_discovery);
}

void *Host::discovery(void *ctx) {
    Host *h = ((Host *) ctx);
    while (h->state == HostState::Discovery) {
        Packet p = Packet(MessageType::SleepServiceDiscovery, 0, 0);
       
        char hostname[BUFFER_SIZE];
        gethostname(hostname, BUFFER_SIZE);
        p.push(hostname);
        
        send_broadcast(p, h->sck_discovery, PORT_DISCOVERY);

        usleep(h->sleep_discovery);
    }
    return 0;
}

