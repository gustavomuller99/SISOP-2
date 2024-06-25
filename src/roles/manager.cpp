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
        // p.print();

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

    if ((m->sck_monitoring = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Manager (Monitoring): ERROR opening socket");
        exit(EXIT_FAILURE); 
    }

    if (setsockopt(m->sck_monitoring, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0) {
        printf("Manager (Monitoring): ERROR setting socket options");
        exit(EXIT_FAILURE);
    }

    // listen(m->sck_monitoring, 5);

    while (1) {
        
        for (const auto& host : m->hosts) {
            std::cout << "IP: " << host.ip << ", State: " << host.state << std::endl;

            struct sockaddr_in guest_addr;

            guest_addr.sin_family = AF_INET;
            guest_addr.sin_port = htons(PORT_MONITORING); // Porta onde o participante está ouvindo
            inet_aton(host.ip.c_str(), &guest_addr.sin_addr);

            if (connect(m->sck_monitoring, (struct sockaddr *)&guest_addr, sizeof(guest_addr)) < 0) {
                // perror("Manager (Monitoring): Error connecting to participant");
                continue;
            } 
            else {
                Packet p = Packet(MessageType::SleepServiceRequest, 0, 0);
                std::string str = p.to_payload();
                const char* _payload = str.c_str();

                // Sending Packet to Host
                if (write(m->sck_monitoring, _payload, strlen(_payload)) < 0) {
                    perror("Manager (Monitoring): Error writing to socket");
                    continue; // Pular para o próximo participante em caso de erro
                }

                // Receiveing Packet from Host
                // char buffer[256];
                // n = read(m->sck_monitoring, buffer, BUFFER_SIZE);
                // if (n < 0) {
                //     perror("Manager (Monitoring): Error receiving from socket\n");
                // }

                // p = Packet(buffer);
                // p.src_ip = inet_ntoa(guest_addr.sin_addr);
                // p.print();
            }
        }
        sleep(1);
    }
    close(m->sck_monitoring);
}