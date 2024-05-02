#include <manager.h>

void Manager::init() {
    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Manager::monitoring, this);
    // pthread_create(&this->t_monitoring, NULL, Manager::management, this);
    // pthread_create(&this->t_monitoring, NULL, Manager::interface, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    // pthread_join(this->management, NULL);
    // pthread_join(this->interface, NULL);
}

void Manager::add_host(KnownHost host) {
    if (!this->has_host(host.name))
        this->hosts.push_back(host);
}

bool Manager::has_host(std::string name) {
    for (KnownHost h: this->hosts) {
        if (h.name == name) return true;
    }
    return false;
}

void *Manager::discovery(void *ctx) {
    Manager *m = ((Manager *) ctx);

    // creating udp server socket file descriptor
    int trueflag = 1;
    struct sockaddr_in recv_addr;

    if ((m->sck_discovery = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        exit(EXIT_FAILURE); 
    
    if (setsockopt(m->sck_discovery, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE); 

    memset(&recv_addr, 0, sizeof recv_addr);

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = (in_port_t) htons(PORT_DISCOVERY);
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m->sck_discovery, (struct sockaddr*) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE); 

    while(1) {
        Packet p = rec_packet(m->sck_discovery);
        p.print();

        // consumes the package 
        std::string hostname = p.pop();

        // discovered new host -> should call management subservice 
        m->add_host({
            p.src_ip,
            hostname,
            HostState::Discovery
        });
    }

    // close socket
    close(m->sck_discovery);
}

void *Manager::monitoring(void *ctx) {
    Manager *m = ((Manager *) ctx);

    int trueflag = 1;
    struct sockaddr_in recv_addr_monitoring;

    if ((m->sck_monitoring = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
        exit(EXIT_FAILURE); 
    
    if (setsockopt(m->sck_monitoring, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE); 

    memset(&recv_addr_monitoring, 0, sizeof recv_addr_monitoring);

    recv_addr_monitoring.sin_family = AF_INET;
    recv_addr_monitoring.sin_port = htons(PORT_MONITORING);
    recv_addr_monitoring.sin_addr.s_addr = INADDR_ANY;

    if (bind(m->sck_monitoring, (struct sockaddr*) &recv_addr_monitoring, sizeof recv_addr_monitoring) < 0)
        exit(EXIT_FAILURE); 

    while (1) {
        Packet p = Packet(MessageType::SleepServiceRequest, 0, 0);
        
        for (const auto& host: m->hosts) {
           std::cout << "IP: " << host.ip << ", Nome: " << host.name << std::endl;
        }
        sleep(1);

    }

    close(m->sck_monitoring);
}