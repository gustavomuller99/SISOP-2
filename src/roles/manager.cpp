#include <manager.h>

void Manager::init() {
    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Manager::monitoring, this);
    pthread_create(&this->t_management, NULL, Manager::management, this);
    pthread_create(&this->t_monitoring, NULL, Manager::interface, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    pthread_join(this->t_management, NULL);
    pthread_join(this->t_interface, NULL);
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

            Packet request = Packet(MessageType::SleepServiceMonitoring, 0, 0);
            send_broadcast_tcp(request, m->sck_monitoring, PORT_MONITORING, host.ip, true);

            Packet response = rec_packet_tcp(m->sck_monitoring);

            if (response.get_type() == MessageType::HostAsleep && host.state == HostState::Awaken) {
                host.state = HostState::Awaken;
                // Call Interface Subservice (change in hosts list)
            }

            else if(response.get_type() == MessageType::HostAwaken && host.state == HostState::Asleep) {
                host.state = HostState::Awaken;
                // Call Interface Subservice (change in hosts list)
            }

            // response.print();
        }
    }
    close(m->sck_monitoring);
    return 0;
}

void* Manager::management(void *ctx) {
    Manager *m = (Manager *)ctx;

    int trueflag = 1;

    if ((m->sck_management = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Manager (Management): ERROR opening socket\n");
        exit(EXIT_FAILURE); 
    }

    if (setsockopt(m->sck_management, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof(trueflag)) < 0) {
        printf("Manager (Management): ERROR setting socket options\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in manager_addr;
    struct sockaddr_in guest_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    memset(&guest_addr, 0, sizeof(guest_addr));
    guest_addr.sin_family = AF_INET;
    guest_addr.sin_port = htons(PORT_MANAGEMENT);
    guest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(m->sck_management, (struct sockaddr*) &guest_addr, addr_len) < 0){
        printf("Manager (Management): ERROR binding\n");
        exit(EXIT_FAILURE);
    }

    listen(m->sck_management, 5);

    while (1) {
        int client_sock;
        if ((client_sock = accept(m->sck_management, (struct sockaddr *) &manager_addr, &addr_len)) < 0) {
            perror("Manager (Management): ERROR on accept");
            exit(EXIT_FAILURE);
        }

        // NOT FOR THIS PART OF THE PROJECT
        // Handle management requests from clients (for example, list hosts and their states)
        // Example: Send current host states to client
        /*for (auto& host : m->hosts) {
            std::string status_str = (host.state == HostState::Awaken) ? "Awake" : "Asleep";
            std::string message = host.name + " is currently " + status_str + "\n";
            write(client_sock, message.c_str(), message.length());
        }*/

        close(client_sock);
    }

    close(m->sck_management);
}

void *Manager::interface(void *ctx) {
    return 0;
}