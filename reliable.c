#define _WIN32_WINNT 0x0600
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "reliable.h"

// ------------------
// get_field_value()
// ------------------
int get_field_value(const char *msg, const char *key, char *out, int outlen) {
    const char *ptr = msg;
    size_t keylen = strlen(key);

    while (*ptr) {
        const char *lineend = strchr(ptr, '\n');
        size_t linelen = lineend ? (size_t)(lineend - ptr) : strlen(ptr);

        if (linelen > keylen + 1 && strncmp(ptr, key, keylen) == 0 && ptr[keylen] == ':') {
            const char *val = ptr + keylen + 1;
            if (*val == ' ') val++;

            size_t vlen = lineend ? (size_t)(lineend - val) : strlen(val);
            if (vlen >= (size_t)outlen) vlen = (size_t)outlen - 1;

            strncpy(out, val, vlen);
            out[vlen] = '\0';
            return 1;
        }

        if (!lineend) break;
        ptr = lineend + 1;
    }

    return 0;
}

// ------------------
// sequence generator
// ------------------
static int next_sequence_number = 1;

int get_next_sequence() {
    return next_sequence_number++;
}

// --------------------------------
// send_with_ack: retransmit logic
// --------------------------------
int send_with_ack(SOCKET sock, struct sockaddr_in *peer, const char *msg, int seq_number) {
    char recvbuf[4096];
    struct sockaddr_in from;
    int retries = 0;

    while (retries < 3) {
        // send datagram
        sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)peer, sizeof(*peer));

        // wait 500ms
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sock, &set);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;

        int ready = select(0, &set, NULL, NULL, &tv);

        if (ready > 0) {
            int fromlen = sizeof(from);
            int n = recvfrom(sock, recvbuf, sizeof(recvbuf) - 1, 0,
                             (struct sockaddr*)&from, &fromlen);

            if (n > 0) {
                recvbuf[n] = '\0';

                char mtype[64], ackstr[64];
                if (get_field_value(recvbuf, "message_type", mtype, sizeof(mtype)) &&
                    strcmp(mtype, "ACK") == 0 &&
                    get_field_value(recvbuf, "ack_number", ackstr, sizeof(ackstr)) &&
                    atoi(ackstr) == seq_number) {

                    return 1; // SUCCESS
                }
            }
        }

        retries++;
        printf("[send_with_ack] Timeout %d/3, resending seq %d...\n", retries, seq_number);
    }

    printf("[send_with_ack] FAILED: no ACK after 3 retries.\n");
    return 0;
}

// ----------------------------
// recv_and_auto_ack
// ----------------------------
int recv_and_auto_ack(SOCKET sock, struct sockaddr_in *from, char *buffer, int buflen, int timeout_ms) {

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000;

    int ready = select(0, &set, NULL, NULL, &tv);
    if (ready <= 0) return 0; // timeout

    int fromlen = sizeof(*from);
    int n = recvfrom(sock, buffer, buflen - 1, 0,
                     (struct sockaddr*)from, &fromlen);

    if (n <= 0) return 0;
    buffer[n] = '\0';

    // extract sequence_number
    char seqstr[64];
    if (get_field_value(buffer, "sequence_number", seqstr, sizeof(seqstr))) {
        int seq = atoi(seqstr);

        // send ACK
        char ackmsg[256];
        sprintf(ackmsg, "message_type: ACK\nack_number: %d\n", seq);

        sendto(sock,
               ackmsg,
               strlen(ackmsg),
               0,
               (struct sockaddr*)from,
               sizeof(*from));
    }

    return n;
}
