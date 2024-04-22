#include <message.h>

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
    std::string top = this->data.front();
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

    // Filling server information
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

