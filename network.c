#include "network.h"
#include <stdio.h>
#include <winsock2.h>

/*
 * Patched network.c
 * Adds:
 *  - SO_RCVTIMEO (3-second receive timeout)
 *  - SO_REUSEADDR for safe rebinding on HOST
 * Fully compatible with protocols.c and battle.c
 */

// Creates and binds a UDP socket into a port
SOCKET create_udp_socket(int port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
    {
        printf("Error creating socket.\n");
        return INVALID_SOCKET;
    }

    // --------------------------------------------
    // Allow quick rebind (helps when restarting HOST)
    // --------------------------------------------
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // --------------------------------------------
    // Add receive timeout to prevent blocking forever
    // --------------------------------------------
    DWORD timeout = 3000;  // 3000 ms = 3 seconds
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        printf("Bind failed.\n");
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

// Sends a UDP message to a specific address
int send_udp(SOCKET sock, const char *msg, struct sockaddr_in *to)
{
    return sendto(sock, msg, strlen(msg), 0,
                  (struct sockaddr*)to, sizeof(*to));
}

// Receives a UDP message with timeout
int recv_udp(SOCKET sock, char *buffer, int max, struct sockaddr_in *from, int *from_len)
{
    int rv = recvfrom(sock, buffer, max, 0,
                      (struct sockaddr*)from, from_len);

    // rv will be SOCKET_ERROR on timeout OR real error
    return rv;
}

// Close socket
void close_udp(SOCKET sock)
{
    closesocket(sock);
}
