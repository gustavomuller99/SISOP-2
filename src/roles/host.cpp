#include <host.h>

void Host::init() {
    pthread_mutex_lock(&this->mutex_ncurses);
    initscr();
    refresh();
    curs_set(false);
    pthread_mutex_unlock(&this->mutex_ncurses);

    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Host::monitoring, this);
    pthread_create(&this->t_interface, NULL, Host::interface, this);
    pthread_create(&this->t_input, NULL, Host::input, this);
    pthread_create(&this->t_election, NULL, Host::election, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_interface, NULL);
    pthread_join(this->t_input, NULL);
    pthread_join(this->t_election, NULL);

    if (this->prev_state == HostState::Discovery) {
        pthread_cancel(this->t_monitoring);
        close(this->sck_monitoring);
    } else pthread_join(this->t_monitoring, NULL);

    pthread_mutex_lock(&this->mutex_ncurses);
    endwin();
    pthread_mutex_unlock(&this->mutex_ncurses);
}

void Host::switch_state(HostState new_state) {
    pthread_mutex_lock(&this->mutex_change_state);
    if (this->state != HostState::Exit) {
        this->prev_state = this->state;
        this->state = new_state;
    }
    pthread_mutex_unlock(&this->mutex_change_state);
}

void Host::exit_handler(int sn, siginfo_t* t, void* ctx) {
    this->switch_state(HostState::Exit);
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
        perror("Host (Monitoring): ERROR on accept");
        exit(EXIT_FAILURE);
    }

    timeval tv;
    tv.tv_sec = tcp_timeout;
    tv.tv_usec = 0;

    if (setsockopt (sck_monitoring, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval)) < 0) {
        perror("Manager (Monitoring): Error setting timeout");
        close(sck_monitoring);
    }

    m_info.ip = inet_ntoa(manager_addr.sin_addr);
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
    
    /* connection established */
    while (1) {
        Packet request = rec_packet_tcp(h->sck_monitoring);

        // if first discovered, switch state
        if (h->state == HostState::Discovery) {
            h->switch_state(HostState::Awaken);
        }

        // timed out == returned from suspend
        if (request.get_type() == MessageType::Error) {
            h->create_monitoring_socket();
            h->manager_out = true;
        }
        // answers only the host current state OR exits
        else if (h->state == HostState::Exit) {
            Packet response = Packet(MessageType::SleepServiceExit, 0, 0);
            send_tcp(response, h->sck_monitoring, PORT_MONITORING);
            break;
        } 
        
        else if (request.get_type() == MessageType::SleepServiceMonitoring) {
            h->manager_out = false;
            std::string manager_mac = request.pop();
            std::string manager_name = request.pop();

            h->m_info.mac = manager_mac;
            h->m_info.name = manager_name;

            Packet response = Packet(MessageType::SleepServiceMonitoring, 0, 0);
            response.push(std::to_string(h->state));
            send_tcp(response, h->sck_monitoring, PORT_MONITORING);
        }
    }

    close(h->sck_monitoring);
    return 0;
}

void *Host::interface(void *ctx) {
    Host *h = ((Host *) ctx);

    WINDOW *output;
    int start_x = 0, start_y = 2, width = 200, height = 4;
    pthread_mutex_lock(&h->mutex_ncurses);
    output = create_newwin(height, width, start_y, start_x);
    pthread_mutex_unlock(&h->mutex_ncurses);

    while (h->state != HostState::Exit) {
        pthread_mutex_lock(&h->mutex_ncurses);
        wclear(output);
        wprintw(output, "Manager Info: (IP) %s (MAC) %s (NAME) %s\n", h->m_info.ip.data(), h->m_info.mac.data(), h->m_info.name.data());
        wprintw(output, "Current host state: %s\n", string_from_state(h->state).data());
        wprintw(output, "Press EXIT to quit");
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

void *Host::election(void *ctx) {
    Host *h = ((Host *) ctx);

    while(h->manager_out){
        usleep(h->election_timeout);
    }

    return 0;
}

/* utils */

HostState state_from_string(std::string state) {
    if (state == "1") return HostState::Discovery;
    if (state == "2") return HostState::Asleep;
    return HostState::Awaken;
}

std::string string_from_state(int state) {
    switch (state) {
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

