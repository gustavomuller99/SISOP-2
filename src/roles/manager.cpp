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

    while (1) {
        
        for (auto& host : m->hosts) {
            std::cout << "IP: " << host.ip << ", State: " << host.state << std::endl;

            struct sockaddr_in guest_addr;

            memset(&guest_addr, 0, sizeof(guest_addr));
            guest_addr.sin_family = AF_INET;
            guest_addr.sin_port = htons(PORT_MONITORING);
            inet_aton(host.ip.c_str(), &guest_addr.sin_addr);

            connect(m->sck_monitoring, (struct sockaddr *)&guest_addr, sizeof(guest_addr));

            Packet request = Packet(MessageType::SleepServiceRequest, 0, 0);
            std::string str = request.to_payload();
            const char* _request = str.c_str();

            // Sending Packet to Host
            if (write(m->sck_monitoring, _request, strlen(_request)) < 0) {
                perror("Manager (Monitoring): Error writing to socket");
                continue;
            }

            // Receiveing Packet from Host
            char buffer[256];
            if (read(m->sck_monitoring, buffer, BUFFER_SIZE) < 0) {
                // If host not responding and status in table == "Awaken"
                if (host.state == 3) {
                    // Change status to "ASLEEP"
                    host.state = HostState::Asleep;
                }
            }
            else {
                // If host is responding and status in table == "Discovery" or status in table == "Asleep"
                if (host.state == HostState::Discovery || host.state == HostState::Asleep) {
                    // Change status to "awaken"
                    host.state = HostState::Awaken;
                }
                Packet response = Packet(buffer);
                response.src_ip = inet_ntoa(guest_addr.sin_addr);
                response.print();
            }

        }
    }
    close(m->sck_monitoring);
    return 0;
}