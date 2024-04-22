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

const int BUFFER_SIZE = 256;

enum MessageType {
    SleepServiceDiscovery = 0,
    SleepServiceRequest = 1
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
private:
    uint16_t type; // MessageType
    uint16_t seqn; 
    uint16_t timestamp;
    std::vector<std::string> data; // stack de dados
};

/* send packet on broadcast using port and socket  */
void send_broadcast(Packet p, int sockfd, int port);

/* waits for packcage and parses into object */
Packet rec_packet(int sockfd);

#endif //_MESSAGE_H