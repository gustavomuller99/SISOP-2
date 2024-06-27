#include <host.h>

void Host::init() {
    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Host::monitoring, this);
    pthread_create(&this->t_interface, NULL, Host::interface, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    pthread_join(this->t_interface, NULL);
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

    if ((h->sck_monitoring = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Host (Monitoring): ERROR opening socket\n");
        exit(EXIT_FAILURE); 
    }

    if (setsockopt(h->sck_monitoring, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof(trueflag)) < 0) {
        printf("Manager (Monitoring): ERROR setting socket timeout");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in manager_addr;
    struct sockaddr_in guest_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    memset(&guest_addr, 0, sizeof(guest_addr));
    guest_addr.sin_family = AF_INET;
    guest_addr.sin_port = htons(PORT_MONITORING);
    guest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(h->sck_monitoring, (struct sockaddr*) &guest_addr, addr_len) < 0){
        printf("Host (Monitoring): ERROR binding\n");
        exit(EXIT_FAILURE);
    }

    listen(h->sck_monitoring, 5);

    if ((h->sck_monitoring = accept(h->sck_monitoring, (struct sockaddr *) &manager_addr, &addr_len)) < 0) {
        perror("Host (Monitoring): ERROR on accept");
        exit(EXIT_FAILURE);
    }

    while (1) {
        char buffer[256];
        const char* _response = nullptr;
        
        // Receiving Packet from Manager
        if (read(h->sck_monitoring, buffer, BUFFER_SIZE) < 0) {
            perror("Host (Monitoring): ERROR reading from socket");
            continue;
        }

        Packet p = Packet(buffer);
        p.src_ip = inet_ntoa(manager_addr.sin_addr);
        // p.print();

        // Sending Packet to Manager
        // If Host is out of service (Because "EXIT" was typed in terminal)
        if(h->host_out) {
            Packet response = Packet(MessageType::HostAsleep, 0, 0);
            std::string str = response.to_payload();
            _response = str.c_str();
        }
        // Host online
        else {
            Packet response = Packet(MessageType::HostAwaken, 0, 0);
            std::string str = response.to_payload();
            _response = str.c_str();
        }

        if (write(h->sck_monitoring, _response, strlen(_response)) < 0) {
            perror("Host (Monitoring): ERROR writing to socket"); 
            continue;
        }
    }
    close(h->sck_monitoring);
    return 0;
}

void *Host::interface(void *ctx) {
    Host *h = ((Host *) ctx);

    std::string input;
    std::cout << "To quit the service type EXIT." << std::endl;
    std::cin >> input;
    if (input == "EXIT")
    {
        pthread_mutex_lock(&h->mutex_host_out);
        h->host_out = true;
        pthread_mutex_unlock(&h->mutex_host_out);
    }
    return 0;
}

