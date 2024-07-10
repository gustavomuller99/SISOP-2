#include <host.h>

void Host::init() {
    initscr();
    refresh();
    curs_set(false);

    pthread_create(&this->t_discovery, NULL, Host::discovery, this);
    pthread_create(&this->t_monitoring, NULL, Host::monitoring, this);
    pthread_create(&this->t_interface, NULL, Host::interface, this);
    pthread_create(&this->t_input, NULL, Host::input, this);

    pthread_join(this->t_discovery, NULL);
    pthread_join(this->t_interface, NULL);
    pthread_join(this->t_input, NULL);

    if (this->prev_state == HostState::Discovery) {
        pthread_kill(this->t_monitoring, 0);
        close(this->sck_monitoring);
    } else pthread_join(this->t_monitoring, NULL);

    endwin();
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

    /* connection established */
    while (1) {
        Packet request = rec_packet_tcp(h->sck_monitoring);

        // if first discovered, switch state
        if (h->state == HostState::Discovery) {
            h->switch_state(HostState::Asleep);
        }

        // answers only the host current state OR exits
        if (h->state == HostState::Exit) {
            Packet response = Packet(MessageType::SleepServiceExit, 0, 0);
            send_tcp(response, h->sck_monitoring, PORT_MONITORING);
            break;
        } else {
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
    int start_x = 0, start_y = 0, width = 50, height = 2;
    output = create_newwin(height, width, start_y, start_x);

    while (h->state != HostState::Exit) {
        wclear(output);
        wprintw(output, "Current host state: %s\n", string_from_state(h->state).data());
        wprintw(output, "Press EXIT to quit", string_from_state(h->state).data());
        wrefresh(output);

        usleep(h->sleep_interface);
    }

    destroy_win(output);
    return 0;
}

void *Host::input(void *ctx) {
    Host *h = ((Host *) ctx);

    WINDOW *input;
    int start_x = 0, start_y = 2, width = 50, height = 1;
    input = create_newwin(height, width, start_y, start_x);
    wclear(input);

    while (h->state != HostState::Exit) {
        char buff[256];
        wgetstr(input, buff);

        if (strcmp(buff, "EXIT") == 0) {
            h->switch_state(HostState::Exit);
        }

        wclear(input);
    }

    destroy_win(input);
    return 0;
}

void Host::switch_state(HostState new_state) {
    pthread_mutex_lock(&this->mutex_change_state);
    if (this->state != HostState::Exit) {
        this->prev_state = this->state;
        this->state = new_state;
    }
    pthread_mutex_unlock(&this->mutex_change_state);
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
    box(local_win, 0, 0);        /* 0, 0 dá caracteres padrão para as linhas verticais and horizontais	*/
    wrefresh(local_win);        /* Mostra aquela caixa 	*/

    return local_win;
}

void destroy_win(WINDOW *local_win) {
    /* box (local_win, '', ''); : Isso não produzirá o resultado
      *  desejado de apagar a janela. Vai deixar seus quatro cantos,
      * e uma lembrança feia da janela.
    */
    wborder(local_win, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ');
    /* Os parâmetros usados são
      * 1. win: a janela na qual operar
      * 2. ls: caractere a ser usado para o lado esquerdo da janela
      * 3. rs: caractere a ser usado para o lado direito da janela
      * 4. ts: caractere a ser usado na parte superior da janela
      * 5. bs: caractere a ser usado na parte inferior da janela
      * 6. tl: caractere a ser usado para o canto superior esquerdo da janela
      * 7. tr: caractere a ser usado no canto superior direito da janela
      * 8. bl: caractere a ser usado no canto inferior esquerdo da janela
      * 9. br: caractere a ser usado no canto inferior direito da janela
      */
    wrefresh(local_win);
    delwin(local_win);
}

