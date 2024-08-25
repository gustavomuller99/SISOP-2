#include <host.h>

void Host::init() {
    pthread_mutex_lock(&this->mutex_ncurses);
    initscr();
    refresh();
    curs_set(false);
    pthread_mutex_unlock(&this->mutex_ncurses);

    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Host::monitoring, this);
    pthread_create(&this->t_check_manager, NULL, Host::check_manager, this);
    pthread_create(&this->t_listen_election, NULL, Host::listen_election, this);
    pthread_create(&this->t_run_election, NULL, Host::run_election, this);
    pthread_create(&this->t_interface, NULL, Host::interface, this);
    pthread_create(&this->t_input, NULL, Host::input, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_check_manager, NULL);
    pthread_join(this->t_listen_election, NULL);
    pthread_join(this->t_run_election, NULL);
    pthread_join(this->t_interface, NULL);
    pthread_join(this->t_input, NULL);

    if (this->prev_state == HostState::Discovery) {
        pthread_cancel(this->t_monitoring);
        close(this->sck_monitoring);
    } else pthread_join(this->t_monitoring, NULL);

    pthread_mutex_lock(&this->mutex_ncurses);
    endwin();
    pthread_mutex_unlock(&this->mutex_ncurses);

    b_should_exit_election = true;
}

void Host::exit_handler(int sn, siginfo_t* t, void* ctx) {
    this->switch_state(HostState::Exit);
}

void Host::switch_state(HostState new_state) {
    pthread_mutex_lock(&this->mutex_change_state);
    if (this->state != HostState::Exit) {
        this->prev_state = this->state;
        this->state = new_state;
    }
    pthread_mutex_unlock(&this->mutex_change_state);
}

void Host::update_election_answer(bool value) {
    pthread_mutex_lock(&this->mutex_answer);
    b_election_answer = value;
    pthread_mutex_unlock(&this->mutex_answer);
}

void Host::create_monitoring_socket() {
    close(sck_monitoring);

    int trueflag = 1;

    if ((sck_monitoring = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Host (Monitoring): ERROR opening socket\n");
        exit(EXIT_FAILURE); 
    }

    if (setsockopt(sck_monitoring, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof(trueflag)) < 0) {
        printf("Manager (Monitoring): ERROR reusing addr");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(sck_monitoring, SOL_SOCKET, SO_REUSEPORT, &trueflag, sizeof(trueflag)) < 0) {
        printf("Manager (Monitoring): ERROR reusing port");
        exit(EXIT_FAILURE);
    }

    timeval tv;
    tv.tv_sec = tcp_timeout;
    tv.tv_usec = 0;

    if (setsockopt (sck_monitoring, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
        perror("Manager (Monitoring): Error setting timeout");
        close(sck_monitoring);
    }

    struct sockaddr_in manager_addr;
    struct sockaddr_in guest_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    memset(&guest_addr, 0, sizeof(guest_addr));
    guest_addr.sin_family = AF_INET;
    guest_addr.sin_port = htons(PORT_MONITORING);
    guest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sck_monitoring, (struct sockaddr*) &guest_addr, addr_len) < 0){
        printf("Host (Monitoring): ERROR binding\n");
        exit(EXIT_FAILURE);
    }

    listen(sck_monitoring, 5);

    if ((sck_monitoring = accept(sck_monitoring, (struct sockaddr *) &manager_addr, &addr_len)) < 0) {
        manager_up = false;
        return;
    }

    manager_up = true;
    m_info.ip = inet_ntoa(manager_addr.sin_addr);
}

std::vector<KnownHost> Host::get_hosts() {
    std::vector<KnownHost> copy;

    pthread_mutex_lock(&mutex_hosts_replica);
    for (KnownHost h: this->hosts_replica) copy.push_back(h);
    pthread_mutex_unlock(&mutex_hosts_replica);

    return copy;
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

        std::string ip = get_ip();
        long id = hash(ip.substr(ip.size() - 3, 3));
        h->election_id = id;
        
        p.push(std::to_string(id));
        char hostname[BUFFER_SIZE];
        gethostname(hostname, BUFFER_SIZE);
        p.push(hostname);
        p.push(get_mac_address());

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
        printf("Manager (Monitoring): ERROR reusing addr");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(h->sck_monitoring, SOL_SOCKET, SO_REUSEPORT, &trueflag, sizeof(trueflag)) < 0) {
        printf("Manager (Monitoring): ERROR reusing port");
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

    timeval tv;
    tv.tv_sec = h->tcp_timeout;
    tv.tv_usec = 0;

    if (setsockopt (h->sck_monitoring, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
        perror("Manager (Monitoring): Error setting timeout");
        close(h->sck_monitoring);
    }

    h->m_info.ip = inet_ntoa(manager_addr.sin_addr);
    h->manager_up = true;
    
    /* connection established */
    while (1) {
        Packet request = rec_packet_tcp(h->sck_monitoring);

        // if first discovered, switch state
        if (h->state == HostState::Discovery) {
            h->switch_state(HostState::Awaken);
        }

        // skip if running election
        if (h->state == HostState::RunElection) {
            continue;
        }

        // timed out (suspend OR manager quit)
        if (request.get_type() == MessageType::Error) {
            h->create_monitoring_socket(); // sets manager_up accordingly
        } 
        // answers only the host current state OR exits
        else if (h->state == HostState::Exit) {
            Packet response = Packet(MessageType::SleepServiceExit, 0, 0);
            send_tcp(response, h->sck_monitoring, PORT_MONITORING);
            break;
        } 
        else if (request.get_type() == MessageType::SleepServiceCommand) {
            //
        } 
        else if (request.get_type() == MessageType::SleepServiceMonitoring) {
            std::string manager_mac = request.pop();
            std::string manager_name = request.pop();

            h->m_info.mac = manager_mac;
            h->m_info.name = manager_name;

            Packet response = Packet(MessageType::SleepServiceMonitoring, 0, 0);
            response.push(std::to_string(h->state));
            send_tcp(response, h->sck_monitoring, PORT_MONITORING);
        } 
        else if (request.get_type() == MessageType::SleepServiecUpdateRM) {
            pthread_mutex_lock(&h->mutex_hosts_replica);

            h->hosts_replica.clear();
            std::string name;
            while ((name = request.pop()) != "") {
                std::string mac = request.pop();
                std::string ip = request.pop();
                HostState state = state_from_string(request.pop());
                long id = stol(request.pop());
                h->hosts_replica.push_back(KnownHost {ip, mac, name, state, false, 0, id});
            }

            pthread_mutex_unlock(&h->mutex_hosts_replica);
        }

        usleep(h->sleep_monitoring);
    }

    close(h->sck_monitoring);
    return 0;
}

void *Host::check_manager(void *ctx) {
    Host *h = ((Host *) ctx);

    /* gives time to discovery subservice */
    usleep(h->sleep_check_manager); 

    while (h->state != HostState::Exit) {
        if (!h->manager_up && h->state != HostState::RunElection) {
            h->switch_state(HostState::RunElection);
        }

        usleep(h->sleep_check_manager);
    }

    return 0;
}

void *Host::listen_election(void *ctx) {
    Host *h = ((Host *) ctx);

    // creating udp server socket file descriptor
    int trueflag = 1;
    struct sockaddr_in recv_addr;

    if ((h->sck_listen = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        exit(EXIT_FAILURE);

    if (setsockopt(h->sck_listen, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE);

    memset(&recv_addr, 0, sizeof recv_addr);

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = (in_port_t) htons(PORT_ELECTION);
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(h->sck_listen, (struct sockaddr *) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE);

    timeval tv;
    tv.tv_sec = h->tcp_timeout;
    tv.tv_usec = 0;

    if (setsockopt (h->sck_listen, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
        perror("Listen (Listen): Error setting timeout");
        close(h->sck_listen);
    }

    while(h->state != HostState::Exit) {
        /*  listen for message 
            needs to be able to read from multiple sources */
        Packet request = rec_packet(h->sck_listen);

        if (request.get_type() == MessageType::Error) {
            continue;
        }
        else if (request.get_type() == MessageType::ElectionServiceAnswer) {
            h->update_election_answer(true);
        } else if (request.get_type() == MessageType::ElectionServiceCoordinator) {
            // process coordinator and switch state to awaken again
            h->switch_state(HostState::Awaken);
            h->manager_up = true;
        } else if (request.get_type() == MessageType::ElectionServiceEletcion) {
            // sends answer message and starts election process
            h->switch_state(HostState::RunElection);

            Packet response = Packet(MessageType::ElectionServiceAnswer, 0, 0);
            send_udp(response, h->sck_election, PORT_ELECTION, request.src_ip);
        }
    }

    close(h->sck_listen);   
    return 0;
}

void *Host::run_election(void *ctx) {
    Host *h = ((Host *) ctx);

    // creating udp socket file descriptor
    int trueflag = 1;

    if ((h->sck_election = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        exit(EXIT_FAILURE);

    while(h->state != HostState::Exit) {
        if (h->state == HostState::RunElection) {
            /*  sends election messages 
                if there is no higher id, sends coordinator message */
            std::vector<KnownHost> hosts_replica_c = h->get_hosts();
            
            for (auto host: hosts_replica_c) {
                if (host.election_id > h->election_id) {
                    Packet response = Packet(MessageType::ElectionServiceEletcion, 0, 0);
                    send_udp(response, h->sck_election, PORT_ELECTION, host.ip);
                }
            }

            // sleeps and checks if any answer message arrived
            usleep(h->sleep_answer);
            if (!h->b_election_answer) {
                // sends coordinator
                Packet response = Packet(MessageType::ElectionServiceCoordinator, 0, 0);
                for (auto host: hosts_replica_c) 
                    send_udp(response, h->sck_election, PORT_ELECTION, host.ip);
                h->b_should_switch_manager = true;
            }
            h->update_election_answer(false);
        }

        usleep(h->sleep_run_election);
    }

    return 0;
}

void *Host::interface(void *ctx) {
    Host *h = ((Host *) ctx);

    WINDOW *output;
    int start_x = 0, start_y = 2, width = 200, height = 50;
    pthread_mutex_lock(&h->mutex_ncurses);
    output = create_newwin(height, width, start_y, start_x);
    pthread_mutex_unlock(&h->mutex_ncurses);

    while (h->state != HostState::Exit) {
        pthread_mutex_lock(&h->mutex_ncurses);
        wclear(output);
        
        wprintw(output, "Manager Info: (IP) %s (MAC) %s (NAME) %s\n", h->m_info.ip.data(), h->m_info.mac.data(), h->m_info.name.data());
        wprintw(output, "Current host state: %s\n", string_from_state(h->state).data());
        wprintw(output, "Current manager state: %s\n", h->manager_up ? "UP" : "DOWN");
        wprintw(output, "Press EXIT to quit\n");

        wprintw(output, "Replica List:\n");

        wmove(output, 5, 0);
        wprintw(output, "Hostname");

        wmove(output, 5, 17);
        wprintw(output, "Endereço IP");

        wmove(output, 5, 37);
        wprintw(output, "Endereço MAC");

        wmove(output, 5, 58);
        wprintw(output, "Status");

        wmove(output, 5, 68);
        wprintw(output, "ID");

        wmove(output, 6, 0);
        for (int i = 0; i < 70; ++i) {
            wprintw(output, "-");
        }

        std::vector<KnownHost> hosts_replica_c = h->get_hosts(); /* copy of hosts replica for printing */
        for (long unsigned int i = 0; i < hosts_replica_c.size(); ++i) {
            auto host = hosts_replica_c[i];
            wmove(output, i + 7, 0);
            wprintw(output, host.name.c_str());

            wmove(output, i + 7, 17);
            wprintw(output, host.ip.c_str());

            wmove(output, i + 7, 37);
            wprintw(output, host.mac.c_str());

            wmove(output, i + 7, 58);
            wprintw(output, string_from_state(host.state).c_str());

            wmove(output, i + 7, 68);
            wprintw(output, std::to_string(host.election_id).c_str());
        }

        wrefresh(output);
        pthread_mutex_unlock(&h->mutex_ncurses);

        usleep(h->sleep_output);
    }

    pthread_mutex_lock(&h->mutex_ncurses);
    destroy_win(output);
    pthread_mutex_unlock(&h->mutex_ncurses);

    return 0;
}

void *Host::input(void *ctx) {
    Host *h = ((Host *) ctx);

    WINDOW *input;
    int start_x = 0, start_y = 0, width = 50, height = 1;
    pthread_mutex_lock(&h->mutex_ncurses);
    input = create_newwin(height, width, start_y, start_x);
    wtimeout(input, h->input_timeout);
    wprintw(input, "> ");
    wmove(input, 0, 2);
    pthread_mutex_unlock(&h->mutex_ncurses);

    std::string in = "";

    while (h->state != HostState::Exit) {
        pthread_mutex_lock(&h->mutex_ncurses);
        char ch = wgetch(input);
        if (ch == '\n') {
            wclear(input);
            if (in == "EXIT") {
                h->switch_state(HostState::Exit);
            }
            in.clear();
            wprintw(input, "> ");
            wmove(input, 0, 2);
        } else if (ch >= 0) {
            in.push_back(ch);
        }
        pthread_mutex_unlock(&h->mutex_ncurses);

        usleep(h->sleep_input);
    }

    pthread_mutex_lock(&h->mutex_ncurses);
    destroy_win(input);
    pthread_mutex_unlock(&h->mutex_ncurses);

    return 0;
}

/* utils */

WINDOW *create_newwin(int height, int width, int starty, int startx) {
    WINDOW *local_win;

    local_win = newwin(height, width, starty, startx);
    wrefresh(local_win);

    return local_win;
}

void destroy_win(WINDOW *local_win) {
    wrefresh(local_win);
    delwin(local_win);
}

