// network.h
#ifndef NETWORK_H
#define NETWORK_H
#include <winsock2.h>

// Creates a UDP socket bound to a given port.
SOCKET create_udp_socket(int port);

// Sends a message using UDP.
int send_udp(SOCKET sock, const char *msg, struct sockaddr_in *to);

// Receives a message using UDP.
int recv_udp(SOCKET sock, char *buffer, int max, struct sockaddr_in *from, int *fromLen);

// Closes a UDP socket.
void close_udp(SOCKET sock);

#endif