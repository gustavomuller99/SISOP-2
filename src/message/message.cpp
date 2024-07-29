#include <message.h>
#include <sys/ioctl.h>
#include <net/if.h>

Packet::Packet(int type, int seqn, int timestamp) {
    this->type = type;
    this->seqn = seqn;
    this->timestamp = timestamp;
}

Packet::Packet(char* buf) {
    std::string str = buf;
    std::string delimiter = "|";

    this->type = str.substr(0, str.find(delimiter))[0] - '0';
    str.erase(0, str.find(delimiter) + delimiter.length());

    this->seqn = str.substr(0, str.find(delimiter))[0] - '0';
    str.erase(0, str.find(delimiter) + delimiter.length());

    this->timestamp = str.substr(0, str.find(delimiter))[0] - '0';
    str.erase(0, str.find(delimiter) + delimiter.length());

    while(!str.empty()) {
        this->data.push_back(str.substr(0, str.find(delimiter)));
        str.erase(0, str.find(delimiter) + delimiter.length());
    }
}

void Packet::push(std::string data) {
    this->data.push_back(data);
}

std::string Packet::pop() {
    if (data.empty()) return "";
    std::string top = this->data.back();
    this->data.pop_back();
    return top;
}

void Packet::print() {
    std::cout << "Packet\n";
    std::cout << "Type: " << this->type << "\n";
    std::cout << "Seqn: " << this->seqn << "\n";
    std::cout << "Timestamp: " << this->timestamp << "\n";
    std::cout << "IP: " << this->src_ip << "\n";
    std::cout << "Data: ";
    for (std::string i : this->data) {
        std::cout << i << " ";
    }
    std::cout << "\n\n";
}

std::string Packet::to_payload() {
    std::string _payload;

    _payload += std::to_string(this->type) + "|";
    _payload += std::to_string(this->seqn) + "|";
    _payload += std::to_string(this->timestamp) + "|";

    for (std::string d : this->data) {
        _payload += d + "|";
    }

    return _payload;
}

/* send/receive funcions */

void send_broadcast(Packet p, int sockfd, int port) {
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = (in_port_t) htons(port);
    serv_addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::string str = p.to_payload();
    const char* _payload = str.c_str();

    sendto(sockfd,
        _payload,
        strlen(_payload),
        MSG_CONFIRM,
        (const struct sockaddr *) &serv_addr,
        sizeof(serv_addr));

    return;
}

void send_tcp(Packet p, int sockfd, int port, std::string ip) {
    std::string str = p.to_payload();
    const char* message = str.c_str();

    if (write(sockfd, message, strlen(message)) < 0) {
        perror("Monitoring: Error writing to socket");
        close(sockfd);
    }

    return;
}

Packet rec_packet(int sockfd) {
    char rbuf[BUFFER_SIZE] = {};

    sockaddr_in rec_addr;
    socklen_t len = sizeof(rec_addr);

    if (recvfrom(sockfd, rbuf, sizeof(rbuf) - 1, 0, (struct sockaddr*) &rec_addr, &len) < 0)
        exit(EXIT_FAILURE);

    Packet p = Packet(rbuf);
    p.src_ip = inet_ntoa(rec_addr.sin_addr);

    return p;
}

Packet rec_packet_tcp(int sockfd) {
    char buffer[BUFFER_SIZE] = {};

    int n = read(sockfd, buffer, BUFFER_SIZE);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::cout << "Manager (Monitoring): Timeout while reading from socket" << std::endl;
            return Packet(MessageType::Error, 0, 0);
        } else {
            perror("Manager (Monitoring): Error reading from socket");
            return Packet(MessageType::Error, 0, 0);
        }
        return Packet(MessageType::Error, 0, 0);
    } else if (n == 0) {
        // Conexão fechada pelo peer
        std::cout << "Manager (Monitoring): Connection closed by peer" << std::endl;
        return Packet(MessageType::Error, 0, 0);
    } else {
        Packet p = Packet(buffer);
        return p;
    }
}

std::string format_mac_address(const unsigned char* mac) {
    char mac_buffer[18];
    snprintf(mac_buffer, sizeof(mac_buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return mac_buffer;
}

std::string get_mac_address() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock < 0) {
        printf("ERROR getting MAC Address\n");
        exit(EXIT_FAILURE); 
    }

    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
        printf("ERROR getting MAC Address\n");
        close(sock);
        exit(EXIT_FAILURE); 
    }

    struct ifreq *it = ifc.ifc_req;
    const struct ifreq *const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it) {
        strncpy(ifr.ifr_name, it->ifr_name, IFNAMSIZ - 1);

        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0
            && !(ifr.ifr_flags & IFF_LOOPBACK)
            && ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                return format_mac_address(mac);
        }
    }

    close(sock);

    printf("ERROR getting MAC Address\n");
    exit(EXIT_FAILURE); 
}
