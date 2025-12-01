#define _WIN32_WINNT 0x0600
#ifndef RELIABLE_H
#define RELIABLE_H

#include <winsock2.h>
#include <ws2tcpip.h>

int get_next_sequence();
int send_with_ack(SOCKET sock, struct sockaddr_in *peer, const char *msg, int seq_number);
int recv_and_auto_ack(SOCKET sock, struct sockaddr_in *from, char *buffer, int buflen, int timeout_ms);

// parser used by reliability layer
int get_field_value(const char *msg, const char *key, char *out, int outlen);

#endif
#ifndef RELIABLE_H
#define RELIABLE_H

#include <winsock2.h>
#include <ws2tcpip.h>

int get_next_sequence();
int send_with_ack(SOCKET sock, struct sockaddr_in *peer, const char *msg, int seq_number);
int recv_and_auto_ack(SOCKET sock, struct sockaddr_in *from, char *buffer, int buflen, int timeout_ms);

// parser used by reliability layer
int get_field_value(const char *msg, const char *key, char *out, int outlen);

#endif
