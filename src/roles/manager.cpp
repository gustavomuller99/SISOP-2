#include <manager.h>

void Manager::init() {
    this->init_discovery();
    this->init_monitoring();
    // this->init_management();
    // this->init_interface();
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

void Manager::init_discovery() {
    // creating udp server socket file descriptor
    int trueflag = 1;
    struct sockaddr_in recv_addr;

    if ((this->sck_discovery = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        exit(EXIT_FAILURE); 
    
    if (setsockopt(this->sck_discovery, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE); 

    memset(&recv_addr, 0, sizeof recv_addr);

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = (in_port_t) htons(PORT_DISCOVERY);
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(this->sck_discovery, (struct sockaddr*) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE); 

    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_join(this->t_discovery, NULL);

    // close socket
    close(this->sck_discovery);
}

void Manager::init_monitoring() {
    // creating tcp server socket file descriptor
    int trueflag = 1;
    struct sockaddr_in recv_addr;

    if ((this->sck_discovery = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
        exit(EXIT_FAILURE); 
    
    if (setsockopt(this->sck_discovery, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE); 

    memset(&recv_addr, 0, sizeof recv_addr);

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(PORT_DISCOVERY); //ver port
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(this->sck_discovery, (struct sockaddr*) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE); 

    // Listen for incoming connections
    if (listen(this->sck_discovery, SOMAXCONN) < 0)
        exit(EXIT_FAILURE);

    pthread_create(&this->t_discovery, NULL, Manager::monitoring, this);
    pthread_join(this->t_discovery, NULL);

    close(this->sck_discovery);
}

void *Manager::discovery(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while(1) {
        Packet p = rec_packet(m->sck_discovery);
        p.print();

        /* consumes the package */
        std::string hostname = p.pop();

        /* discovered new host -> should call management subservice */
        m->add_host({
            p.src_ip,
            hostname,
            HostState::Discovery
        });
    }

    return 0;
}

void *Manager::monitoring(void *ctx) {
    Manager *m = ((Manager *) ctx);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    client_addr.sin_family = AF_INET;
    
    while (1) {
        close(client_socket);
    }
}
