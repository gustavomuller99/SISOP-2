#ifndef _MESSAGE_H
#define _MESSAGE_H

#include <bits/stdc++.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 

/* 30000 - 34999 */
const int PORT_DISCOVERY = 30000;
const int PORT_MONITORING = 31000;

const int BUFFER_SIZE = 256;

enum MessageType {
    SleepServiceDiscovery = 0,
    SleepServiceMonitoring = 1,
    HostAwaken = 2,
    HostAsleep = 3
};

// Descrição dos protocolos:
/*
 * SleepServiceDiscovery:
 * Type|Seqn|Timestamp|Hostname 
 */

class Packet {
public:
    Packet(int type, int seqn, int timestamp); // construtor do pacote na origem
    Packet(char* buf); // construtor do pacote na chegada

    void push(std::string data);
    std::string pop();
    void print();
    std::string to_payload();

    std::string src_ip;
    std::string src_mac;

    MessageType get_type() const {
        return static_cast<MessageType>(type);
    }
private:
    uint16_t type; // MessageType
    uint16_t seqn; 
    uint16_t length;
    uint16_t timestamp;
    std::vector<std::string> data; // stack de dados
};

/* functions to get mac address of current user */
std::string format_mac_address(const unsigned char* mac);
std::string get_mac_address();

/* send packet on broadcast using port and socket  */
void send_broadcast(Packet p, int sockfd, int port);
void send_broadcast_tcp(Packet p, int sockfd, int port, std::string ip = "", bool is_manager = false);

/* waits for packcage and parses into object */
Packet rec_packet(int sockfd);
Packet rec_packet_tcp(int sockfd);

#endif //_MESSAGE_H