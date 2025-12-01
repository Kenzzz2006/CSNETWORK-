#define _WIN32_WINNT 0x0600
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "reliable.h"

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in me = {0};
    me.sin_family = AF_INET;
    me.sin_port = htons(5000);
    me.sin_addr.s_addr = INADDR_ANY;

    bind(sock, (struct sockaddr*)&me, sizeof(me));
    printf("Receiver ready (port 5000)...\n");

    char buffer[4096];
    struct sockaddr_in from;

    while (1) {
        int n = recv_and_auto_ack(sock, &from, buffer, sizeof(buffer), 5000);
        if (n > 0) {
            printf("Received message:\n%s\n", buffer);
        }
    }
}
