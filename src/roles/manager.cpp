#include <manager.h>

void Manager::init() {
    pthread_mutex_lock(&this->mutex_ncurses);
    initscr();
    refresh();
    curs_set(false);
    pthread_mutex_unlock(&this->mutex_ncurses);

    pthread_create(&this->t_discovery, NULL, Manager::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Manager::monitoring, this);
    pthread_create(&this->t_update_rm, NULL, Manager::update_rm, this);
    pthread_create(&this->t_command, NULL, Manager::command, this);
    pthread_create(&this->t_listen_election, NULL, Manager::listen_election, this);
    pthread_create(&this->t_run_election, NULL, Manager::run_election, this);
    pthread_create(&this->t_interface, NULL, Manager::interface, this);
    pthread_create(&this->t_input, NULL, Manager::input, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_monitoring, NULL);
    pthread_join(this->t_update_rm, NULL);
    pthread_join(this->t_command, NULL);
    pthread_join(this->t_listen_election, NULL);
    pthread_join(this->t_run_election, NULL);
    pthread_join(this->t_interface, NULL);
    pthread_join(this->t_input, NULL);

    close(this->sck_discovery);
    for (auto h : this->hosts) {
        if (h.connected) close(h.sockfd);
    }

    pthread_mutex_lock(&this->mutex_ncurses);
    endwin();
    pthread_mutex_unlock(&this->mutex_ncurses);

    b_should_exit_election = true;
}

void Manager::exit_handler(int sn, siginfo_t* t, void* ctx) {
    b_should_exit = true;
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
    return {cmd, host};
}

void Manager::send_wake_on_lan_packet(std::string mac_address) {
    char comando[256];
    snprintf(comando, sizeof(comando), "wakeonlan %s", mac_address.c_str());

    FILE* fp = popen(comando, "r");
    if (fp == NULL) {
        perror("Erro ao enviar WOL.");
        return;
    }
    pclose(fp);
}

void Manager::update_election_answer(bool value) {
    pthread_mutex_lock(&this->mutex_answer);
    b_election_answer = value;
    pthread_mutex_unlock(&this->mutex_answer);
}

void Manager::update_running_election(bool value) {
    pthread_mutex_lock(&this->mutex_running_election);
    b_running_election = value;
    pthread_mutex_unlock(&this->mutex_running_election);
}

void *Manager::discovery(void *ctx) {
    Manager *m = ((Manager *) ctx);

    std::string ip = get_ip();
    long id = hash(ip.substr(ip.size() - 3, 3));
    m->election_id = id;

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

    while (!m->b_should_exit) {
        Packet p = rec_packet(m->sck_discovery);

        // consumes the package
        std::string mac = p.pop();
        std::string hostname = p.pop();
        std::string id = p.pop();
        if (hostname.empty()) hostname = p.src_ip;

        // discovered new host -> should call management subservice 
        m->add_host({
            p.src_ip,
            mac,
            hostname,
            HostState::Discovery,
            false,
            0,
            stol(id)
        });
    }

    // close socket
    close(m->sck_discovery);
}

void *Manager::monitoring(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while (!m->b_should_exit) {
        std::vector<KnownHost> remove;

        // lock so no changes are made to host list during status update
        pthread_mutex_lock(&m->hosts_mutex);

        int failed_conn = 0;

        for (auto it = m->hosts.begin(); it != m->hosts.end(); it++) {
            KnownHost &host = *it;

            if (!host.connected) {
                int sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    perror("Manager (Monitoring): Error creating socket");
                    continue;
                }

                timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = m->tcp_timeout;

                if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
                    perror("Manager (Monitoring): Error setting timeout");
                    close(sockfd);
                    continue;
                }

                if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
                    perror("Manager (Monitoring): Error setting timeout");
                    close(sockfd);
                    continue;
                }

                struct sockaddr_in guest_addr;
                memset(&guest_addr, 0, sizeof(guest_addr));
                guest_addr.sin_family = AF_INET;
                guest_addr.sin_port = htons(PORT_MONITORING);
                inet_aton(host.ip.c_str(), &guest_addr.sin_addr);

                if (connect(sockfd, (struct sockaddr *) &guest_addr, sizeof(guest_addr)) < 0) {
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

            if (response.get_type() == MessageType::Error) {
                host.state = HostState::Asleep;
                host.connected = false;
                close(host.sockfd);
                failed_conn++;
            } else if (response.get_type() == MessageType::SleepServiceExit) {
                // Handle host exit
                remove.push_back(*it);
            } else {
                host.state = HostState::Awaken;
            }
        }

        pthread_mutex_unlock(&m->hosts_mutex);

        if (failed_conn != 0 && failed_conn == (int) m->hosts.size()) {
            m->failed_count++;
        } else m->failed_count = 0;

        if (m->failed_count == 10) {
            m->b_should_switch_host = true;
            m->b_should_try_election = true;
        }

        for (auto r: remove) {
            m->remove_host(r);
        }

        usleep(m->sleep_monitoring);
    }
    return 0;
}

void *Manager::update_rm(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while (!m->b_should_exit) {

        /* get copy of hosts for update */
        std::vector<KnownHost> hosts_c = m->get_hosts();

        // lock so no changes are made to host list during replicas update
        pthread_mutex_lock(&m->hosts_mutex);
        
        for (auto it = m->hosts.begin(); it != m->hosts.end(); it++) {
            KnownHost &host = *it;

            if (!host.connected) continue;
            
            Packet request = Packet(MessageType::SleepServiecUpdateRM, 0, 0);
            for (KnownHost copy: hosts_c) {
                request.push(std::to_string(copy.election_id));
                request.push(string_from_state(copy.state));
                request.push(copy.ip);
                request.push(copy.mac);
                request.push(copy.name);
            }

            send_tcp(request, host.sockfd, PORT_MONITORING, host.ip);
        }

        pthread_mutex_unlock(&m->hosts_mutex);
        usleep(m->sleep_update);
    }
}

void *Manager::command(void *ctx) {
    Manager *m = ((Manager *) ctx);

    while (!m->b_should_exit) {

        while(!m->cmd.empty()) {
            std::pair<int, std::string> send_cmd = m->cmd.front();
            m->cmd.pop_front();
            if (m->has_host(send_cmd.second)) {
                // lock so no changes are made to host list during command send
                pthread_mutex_lock(&m->hosts_mutex);

                for (auto it = m->hosts.begin(); it != m->hosts.end(); it++) {
                    KnownHost &host = *it;
                    if (host.name == send_cmd.second) {
                        if (send_cmd.first == CommandType::Wakeup) {
                            m->send_wake_on_lan_packet(host.mac);
                        }
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

void *Manager::listen_election(void *ctx) {
    Manager *m = ((Manager *) ctx);

    // creating udp server socket file descriptor
    int trueflag = 1;
    struct sockaddr_in recv_addr;

    if ((m->sck_listen = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        exit(EXIT_FAILURE);

    if (setsockopt(m->sck_listen, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) < 0)
        exit(EXIT_FAILURE);

    memset(&recv_addr, 0, sizeof recv_addr);

    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = (in_port_t) htons(PORT_ELECTION);
    recv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m->sck_listen, (struct sockaddr *) &recv_addr, sizeof recv_addr) < 0)
        exit(EXIT_FAILURE);

    timeval tv;
    tv.tv_sec = m->tcp_timeout;
    tv.tv_usec = 0;

    if (setsockopt (m->sck_listen, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
        perror("Listen (Listen): Error setting timeout");
        close(m->sck_listen);
    }

    while(!m->b_should_exit) {
        /*  listen for message 
            needs to be able to read from multiple sources */
        Packet request = rec_packet(m->sck_listen);

        if (request.get_type() == MessageType::Error) {
            continue;
        }
        else if (request.get_type() == MessageType::ElectionServiceAnswer) {
            m->update_election_answer(true);
        } else if (request.get_type() == MessageType::ElectionServiceCoordinator) {
            // process coordinator and switch state to awaken again
            m->b_should_switch_host = true;
        } else if (request.get_type() == MessageType::ElectionServiceEletcion) {
            // sends answer message and starts election process
            request.print();
            exit(0);
            m->update_running_election(true);

            Packet response = Packet(MessageType::ElectionServiceAnswer, 0, 0);
            send_udp(response, m->sck_election, PORT_ELECTION, request.src_ip);
        }
    }

    close(m->sck_listen);   
    return 0;
}

void *Manager::run_election(void *ctx) {
    Manager *m = ((Manager *) ctx);

    // creating udp socket file descriptor
    int trueflag = 1;

    if ((m->sck_election = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        exit(EXIT_FAILURE);

    while(!m->b_should_exit) {
        if (m->b_running_election) {
            /*  sends election messages 
                if there is no higher id, sends coordinator message */
            std::vector<KnownHost> hosts_replica_c = m->get_hosts();
            
            for (auto host: hosts_replica_c) {
                if (host.election_id > m->election_id) {
                    Packet response = Packet(MessageType::ElectionServiceEletcion, 0, 0);
                    send_udp(response, m->sck_election, PORT_ELECTION, host.ip);
                }
            }

            // sleeps and checks if any answer message arrived
            usleep(m->sleep_answer);
            if (!m->b_election_answer) {
                // sends coordinator
                Packet response = Packet(MessageType::ElectionServiceCoordinator, 0, 0);
                for (auto host: hosts_replica_c) 
                    send_udp(response, m->sck_election, PORT_ELECTION, host.ip);
                m->update_running_election(false);
            }
            m->update_election_answer(false);
        }

        usleep(m->sleep_run_election);
    }

    return 0;
}

void *Manager::interface(void *ctx) {
    Manager *m = ((Manager *) ctx);

    WINDOW *output;
    int start_x = 0, start_y = 2, width = 200, height = 50;
    pthread_mutex_lock(&m->mutex_ncurses);
    output = create_newwin(height, width, start_y, start_x);
    pthread_mutex_unlock(&m->mutex_ncurses);

    while (!m->b_should_exit) {
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

        wmove(output, 0, 68);
        wprintw(output, "ID");

        wmove(output, 1, 0);
        for (int i = 0; i < 70; ++i) {
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

            wmove(output, i + 2, 68);
            wprintw(output, std::to_string(host.election_id).c_str());
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

    while (!m->b_should_exit) {
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