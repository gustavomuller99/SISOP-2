#include <manager.h>

void Manager::init() {
    pthread_mutex_lock(&this->mutex_ncurses);
    initscr();
    refresh();
    curs_set(false);
    pthread_mutex_unlock(&this->mutex_ncurses);

    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Manager::monitoring, this);
    pthread_create(&this->t_management, NULL, Manager::management, this);
    pthread_create(&this->t_command, NULL, Manager::command, this);
    pthread_create(&this->t_interface, NULL, Manager::interface, this);
    pthread_create(&this->t_input, NULL, Manager::input, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    pthread_join(this->t_management, NULL);
    pthread_join(this->t_command, NULL);
    pthread_join(this->t_interface, NULL);
    pthread_join(this->t_input, NULL);

    pthread_mutex_lock(&this->mutex_ncurses);
    endwin();
    pthread_mutex_unlock(&this->mutex_ncurses);
}

void Manager::exit_handler(int sn, siginfo_t* t, void* ctx) {
    pthread_cancel(this->t_discovery);
    pthread_cancel(this->t_monitoring);
    pthread_cancel(this->t_management);
    pthread_cancel(this->t_command);
    pthread_cancel(this->t_interface);
    pthread_cancel(this->t_input);
    close(this->sck_discovery);
    close(this->sck_management);
    for (auto h : this->hosts) {
        if (h.connected) close(h.sockfd);
    }
    endwin();
    exit(0);
}

void Manager::add_host(KnownHost host) {
    pthread_mutex_lock(&hosts_mutex);
    if (!this->has_host(host.name)) {
        this->hosts.push_back(host);
    }
    pthread_mutex_unlock(&hosts_mutex);
}

void Manager::remove_host(KnownHost host) {
    pthread_mutex_lock(&hosts_mutex);
    for (auto it = this->hosts.begin(); it != this->hosts.end();) {
        KnownHost &h = *it;
        if (h.name == host.name) {
            close(host.sockfd);
            this->hosts.erase(it);
        } else it++;
    }
    pthread_mutex_unlock(&hosts_mutex);
}

bool Manager::has_host(std::string name) {
    for (KnownHost h: this->hosts) {
        if (h.name == name) return true;
    }
    return false;
}

std::vector<KnownHost> Manager::get_hosts() {
    std::vector<KnownHost> copy;

    pthread_mutex_lock(&hosts_mutex);
    for (KnownHost h: this->hosts) copy.push_back(h);
    pthread_mutex_unlock(&hosts_mutex);

    return copy;
}

std::pair<int, std::string> Manager::check_input(std::string input) {
    std::string wakeup = "WAKEUP";
    std::string sleep = "SLEEP";
    std::string host = "";
    int cmd;
    {
        bool is_wakeup = true;
        if (wakeup.size() > input.size()) is_wakeup = false;
        for (unsigned long i = 0; i < std::min(input.size(), wakeup.size()); ++i) {
            if (input[i] != wakeup[i]) is_wakeup = false;
        }
        if (is_wakeup) {
            for (unsigned long i = wakeup.size() + 1; i < input.size(); ++i) {
                host.push_back(input[i]);
            }
            cmd = CommandType::Wakeup;
        }
    }
    {
        bool is_sleep = true;
        if (sleep.size() > input.size()) is_sleep = false;
        for (unsigned long i = 0; i < std::min(input.size(), sleep.size()); ++i) {
            if (input[i] != sleep[i]) is_sleep = false;
        }
        if (is_sleep) {
            for (unsigned long i = sleep.size() + 1; i < input.size(); ++i) {
                host.push_back(input[i]);
            }
            cmd = CommandType::Sleep;
        }
    }
    return {cmd, host};
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

    if (bind(m->sck_discovery, (struct sockaddr *) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE);

    while (1) {
        Packet p = rec_packet(m->sck_discovery);

        // consumes the package
        std::string mac = p.pop();
        std::string hostname = p.pop();
        if (hostname.empty()) hostname = p.src_ip;

        // discovered new host -> should call management subservice 
        m->add_host({
            p.src_ip,
            mac,
            hostname,
            HostState::Discovery,
            false
        });
    }

    // close socket
    close(m->sck_discovery);
}

void *Manager::monitoring(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while (1) {
        std::vector<KnownHost> remove;

        // lock so no changes are made to host list during status update
        pthread_mutex_lock(&m->hosts_mutex);

        for (auto it = m->hosts.begin(); it != m->hosts.end(); it++) {
            KnownHost &host = *it;

            if (!host.connected) {
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    perror("Manager (Monitoring): Error creating socket");
                    continue;
                }

                struct sockaddr_in guest_addr;
                memset(&guest_addr, 0, sizeof(guest_addr));
                guest_addr.sin_family = AF_INET;
                guest_addr.sin_port = htons(PORT_MONITORING);
                inet_aton(host.ip.c_str(), &guest_addr.sin_addr);

                if (connect(sockfd, (struct sockaddr *) &guest_addr, sizeof(guest_addr)) < 0) {
                    perror("Manager (Monitoring): Error connecting to host");
                    close(sockfd);
                    continue;
                }

                // Update host state to connected
                host.connected = true;
                host.sockfd = sockfd;
            }

            Packet request = Packet(MessageType::SleepServiceMonitoring, 0, 0);

            char hostname[BUFFER_SIZE];
            gethostname(hostname, BUFFER_SIZE);
            request.push(hostname);
            request.push(get_mac_address());

            send_tcp(request, host.sockfd, PORT_MONITORING, host.ip);

            Packet response = rec_packet_tcp(host.sockfd);

            if (response.get_type() == MessageType::SleepServiceExit) {
                // Handle host exit
                remove.push_back(*it);
            } else {
                std::string state = response.pop();
                host.state = state_from_string(state);
            }
        }

        pthread_mutex_unlock(&m->hosts_mutex);

        for (auto r: remove) {
            m->remove_host(r);
        }

        usleep(m->sleep_monitoring);
    }
    return 0;
}

void *Manager::management(void *ctx) {
    return 0;
//    Manager *m = (Manager *)ctx;
//
//    int trueflag = 1;
//
//    if ((m->sck_management = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//        printf("Manager (Management): ERROR opening socket\n");
//        exit(EXIT_FAILURE);
//    }
//
//    if (setsockopt(m->sck_management, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof(trueflag)) < 0) {
//        printf("Manager (Management): ERROR setting socket options\n");
//        exit(EXIT_FAILURE);
//    }
//
//    struct sockaddr_in manager_addr;
//    struct sockaddr_in guest_addr;
//    socklen_t addr_len = sizeof(struct sockaddr_in);
//
//    memset(&guest_addr, 0, sizeof(guest_addr));
//    guest_addr.sin_family = AF_INET;
//    guest_addr.sin_port = htons(PORT_MANAGEMENT);
//    guest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
//
//    if (bind(m->sck_management, (struct sockaddr*) &guest_addr, addr_len) < 0){
//        printf("Manager (Management): ERROR binding\n");
//        exit(EXIT_FAILURE);
//    }
//
//    listen(m->sck_management, 5);
//
//    while (1) {
//        int client_sock;
//        if ((client_sock = accept(m->sck_management, (struct sockaddr *) &manager_addr, &addr_len)) < 0) {
//            perror("Manager (Management): ERROR on accept");
//            exit(EXIT_FAILURE);
//        }
//
//        // NOT FOR THIS PART OF THE PROJECT
//        // Handle management requests from clients (for example, list hosts and their states)
//        // Example: Send current host states to client
//        /*for (auto& host : m->hosts) {
//            std::string status_str = (host.state == HostState::Awaken) ? "Awake" : "Asleep";
//            std::string message = host.name + " is currently " + status_str + "\n";
//            write(client_sock, message.c_str(), message.length());
//        }*/
//
//        close(client_sock);
//    }
//
//    close(m->sck_management);
}

void *Manager::command(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while (1) {

        while(!m->cmd.empty()) {
            std::pair<int, std::string> send_cmd = m->cmd.front();
            m->cmd.pop_front();
            if (m->has_host(send_cmd.second)) {
                // lock so no changes are made to host list during command send
                pthread_mutex_lock(&m->hosts_mutex);

                for (auto it = m->hosts.begin(); it != m->hosts.end(); it++) {
                    KnownHost &host = *it;
                    if (host.name == send_cmd.second) {
                        if (host.connected) {
                            Packet command = Packet(MessageType::SleepServiceCommand, 0, 0);
                            command.push(std::to_string(send_cmd.first));
                            send_tcp(command, host.sockfd, PORT_MONITORING, host.ip);
                        }
                        break;
                    }
                }

                pthread_mutex_unlock(&m->hosts_mutex);
            }
        }

        usleep(m->sleep_command);
    }
}

void *Manager::interface(void *ctx) {
    Manager *m = ((Manager *) ctx);

    WINDOW *output;
    int start_x = 0, start_y = 2, width = 200, height = 50;
    pthread_mutex_lock(&m->mutex_ncurses);
    output = create_newwin(height, width, start_y, start_x);
    pthread_mutex_unlock(&m->mutex_ncurses);

    while (1) {
        pthread_mutex_lock(&m->mutex_ncurses);

        wclear(output);
        wmove(output, 0, 0);
        wprintw(output, "Hostname");

        wmove(output, 0, 17);
        wprintw(output, "Endereço IP");

        wmove(output, 0, 37);
        wprintw(output, "Endereço MAC");

        wmove(output, 0, 58);
        wprintw(output, "Status");

        wmove(output, 1, 0);
        for (int i = 0; i < 64; ++i) {
            wprintw(output, "-");
        }

        /* get copy of hosts, good enough for printing */
        std::vector<KnownHost> hosts_c = m->get_hosts();

        for (long unsigned int i = 0; i < hosts_c.size(); ++i) {
            auto host = hosts_c[i];
            wmove(output, i + 2, 0);
            wprintw(output, host.name.c_str());

            wmove(output, i + 2, 17);
            wprintw(output, host.ip.c_str());

            wmove(output, i + 2, 37);
            wprintw(output, host.mac.c_str());

            wmove(output, i + 2, 58);
            wprintw(output, string_from_state(host.state).c_str());
        }

        wrefresh(output);
        pthread_mutex_unlock(&m->mutex_ncurses);

        usleep(m->sleep_output);
    }

    return 0;
}

void *Manager::input(void *ctx) {
    Manager *m = ((Manager *) ctx);

    WINDOW *input;
    int start_x = 0, start_y = 0, width = 50, height = 1;
    pthread_mutex_lock(&m->mutex_ncurses);
    input = create_newwin(height, width, start_y, start_x);
    wtimeout(input, m->input_timeout);
    wprintw(input, "> ");
    wmove(input, 0, 2);
    pthread_mutex_unlock(&m->mutex_ncurses);

    std::string in = "";

    while (1) {
        pthread_mutex_lock(&m->mutex_ncurses);
        char ch = wgetch(input);
        if (ch == '\n') {
            /* check for wakeup command */
            std::pair<int, std::string> ret = m->check_input(in);
            if (!ret.second.empty()) {
                m->cmd.push_back(ret);
            }
            wclear(input);
            in.clear();
            wprintw(input, "> ");
            wmove(input, 0, 2);
        } else if (ch >= 0) {
            in.push_back(ch);
        }
        pthread_mutex_unlock(&m->mutex_ncurses);

        usleep(m->sleep_input);
    }

    pthread_mutex_lock(&m->mutex_ncurses);
    destroy_win(input);
    pthread_mutex_unlock(&m->mutex_ncurses);

    return 0;
}