#include <manager.h>

void Manager::init() {
    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Manager::monitoring, this);
    pthread_create(&this->t_interface, NULL, Manager::interface, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    pthread_join(this->t_interface, NULL);
}

std::string Manager::state_string(KnownHost host) {
    switch (host.state) {
        case HostState::Discovery:
            return "Discovery";
        case HostState::Asleep:
            return "Asleep";
        case HostState::Awaken:
            return "Awaken";
        default:
            return "Unknown";
    }
}

void Manager::add_host(KnownHost host) {
    if (!this->has_host(host.name)) {
        this->hosts.push_back(host);
        this->print_hosts();
    }
}

bool Manager::has_host(std::string name) {
    for (KnownHost h: this->hosts) {
        if (h.name == name) return true;
    }
    return false;
}

void Manager::print_hosts() {
    std::cout << "\n" << std::endl;

    std::cout << std::left << std::setw(20) << "Hostname"
              << std::setw(15) << "IP"
              << "Status" << std::endl;

    std::cout << std::setw(20) << std::setfill('-') << ""
              << std::setw(15) << ""
              << std::setfill(' ') << "-------" << std::endl;

    for (const KnownHost &h : this->hosts) {
        std::cout << std::left << std::setw(20) << h.name
                  << std::setw(15) << h.ip
                  << this->state_string(h) << "\n" << std::endl;
    }
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
        
        for (KnownHost &host : m->hosts) {

            Packet request = Packet(MessageType::SleepServiceMonitoring, 0, 0);
            send_broadcast_tcp(request, m->sck_monitoring, PORT_MONITORING, host.ip, true);

            Packet response = rec_packet_tcp(m->sck_monitoring);

            if (response.get_type() == MessageType::HostAsleep && host.state == HostState::Awaken) {
                host.state = HostState::Asleep;
                m->print_hosts();
            }

            else if(response.get_type() == MessageType::HostAwaken && host.state == HostState::Asleep) {
                host.state = HostState::Awaken;
                m->print_hosts();
            }

            // TO-DO: Create MessageType::Timeout
            // else if(response.get_type() == MessageType::Timeout && host.state == HostState::Awaken) {
            //     host.state = HostState::Asleep;
            //     m->print_hosts();
            // }

        }
    }
    close(m->sck_monitoring);
    return 0;
}

void *Manager::interface(void *ctx) {
    return 0;
}