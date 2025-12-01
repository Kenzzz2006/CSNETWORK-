#define _WIN32_WINNT 0x0600
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "reliable.h"

int main() {
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(5000);
    inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);

    int seq = get_next_sequence();

    char msg[256];
    sprintf(msg,
            "message_type: TEST\npayload: hello\nsequence_number: %d\n",
            seq);

    printf("Sending seq %d...\n", seq);

    int ok = send_with_ack(sock, &peer, msg, seq);

    if (ok) printf("ACK received!\n");
    else    printf("NO ACK after 3 retries.\n");
}
