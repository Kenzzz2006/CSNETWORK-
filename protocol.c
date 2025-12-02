/*
 * protocols.c (Fully fixed drop-in)
 *
 * - Adds a reliable receive wrapper with retries to tolerate UDP loss/timeouts
 * - Keeps existing semantics (returns 1 on success, 0 on failure)
 * - Detects GAME_OVER early and exits cleanly
 * - Compatible with provided protocols.h and battle.c
 *
 * Paste this file over your existing protocols.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>

#include "protocols.h"
#include "network.h"

/* Broadcast configuration (shared) */
int g_use_broadcast = 0;
struct sockaddr_in g_broadcast_addr;

/* Default retry count for reliable receives */
#define RECV_MAX_RETRIES 10

/* ---------------------------------------------------------
   Helper: Check if a received buffer has message_type: GAME_OVER
--------------------------------------------------------- */
int detect_game_over(const char *buffer, char *winner, char *loser, int *seq_num)
{
    if (!buffer) return 0;
    if (strstr(buffer, "message_type: GAME_OVER") == NULL)
        return 0;

    char *p;

    if (winner) winner[0] = '\0';
    if (loser) loser[0] = '\0';
    if (seq_num) *seq_num = 0;

    p = strstr(buffer, "winner:");
    if (p && winner) sscanf(p, "winner: %49s", winner);

    p = strstr(buffer, "loser:");
    if (p && loser) sscanf(p, "loser: %49s", loser);

    p = strstr(buffer, "sequence_number:");
    if (p && seq_num) sscanf(p, "sequence_number: %d", seq_num);

    return 1;
}

/* ---------------------------------------------------------
   Reliable UDP receive with retries
   Returns:
     >0 = bytes received (same semantics as recv_udp)
     -1 = failed after retries
--------------------------------------------------------- */
int recv_udp_reliable(SOCKET sock, char *buffer, int max,
                      struct sockaddr_in *peer, int *from_len,
                      int max_retries)
{
    if (!buffer) return -1;
    if (max_retries <= 0) max_retries = RECV_MAX_RETRIES;

    for (int attempt = 1; attempt <= max_retries; attempt++)
    {
        int rv = recv_udp(sock, buffer, max, peer, from_len);
        if (rv > 0) {
            return rv;
        }

        /* If recv_udp returned <= 0, log and retry (timeout or error) */
        /* Use WSAGetLastError only if SOCKET_ERROR was reported by recv_udp */
        printf("[NET] recv attempt %d/%d returned %d. Retrying...\n",
               attempt, max_retries, rv);
        fflush(stdout);
    }

    printf("[NET] recv failed after %d attempts.\n", max_retries);
    fflush(stdout);
    return -1;
}

/* ---------------------------------------------------------
   HANDSHAKE
   (keep some retries here too)
--------------------------------------------------------- */
int perform_handshake(SOCKET sock, ROLE role, struct sockaddr_in *peer, int *seed)
{
    char buffer[512];
    int from_len = sizeof(*peer);

    /* Give handshake more time: increase socket recv timeout inside caller if needed */
    if (role == HOST)
    {
        printf("Waiting for Joiner or Spectator handshake...\n");
        fflush(stdout);

        for (int attempt = 1; attempt <= 6; attempt++)
        {
            int rv = recv_udp_reliable(sock, buffer, sizeof(buffer)-1, peer, &from_len, 1);
            if (rv > 0)
            {
                buffer[rv] = '\0';
                printf("Handshake request received:\n%s\n", buffer);
                fflush(stdout);

                if (strstr(buffer, "HANDSHAKE_REQUEST") || strstr(buffer, "SPECTATOR_REQUEST"))
                {
                    srand((unsigned)time(NULL));
                    *seed = rand() % 100000;

                    snprintf(buffer, sizeof(buffer),
                             "message_type: HANDSHAKE_RESPONSE\n"
                             "seed: %d\n",
                             *seed);

                    send_udp(sock, buffer, &g_broadcast_addr);
                    return 1;
                }

                printf("Invalid handshake request.\n");
                fflush(stdout);
            }
            else {
                printf("No handshake received (attempt %d/6)...\n", attempt);
                fflush(stdout);
            }
        }

        return 0;
    }
    else
    {
        const char *handshakeType =
            (role == JOINER)
            ? "message_type: HANDSHAKE_REQUEST\n"
            : "message_type: SPECTATOR_REQUEST\n";

        for (int attempt = 1; attempt <= 6; attempt++)
        {
            send_udp(sock, handshakeType, peer);

            int rv = recv_udp_reliable(sock, buffer, sizeof(buffer)-1, peer, &from_len, 1);
            if (rv > 0)
            {
                buffer[rv] = '\0';
                printf("Handshake response received:\n%s\n", buffer);
                fflush(stdout);

                char *p = strstr(buffer, "seed:");
                if (p)
                {
                    *seed = atoi(p + 5);
                    return 1;
                }

                printf("Malformed handshake response.\n");
                fflush(stdout);
            }
            else {
                printf("No handshake response (attempt %d/6)...\n", attempt);
                fflush(stdout);
            }
        }

        return 0;
    }
}

/* ---------------------------------------------------------
   BATTLE_SETUP
--------------------------------------------------------- */
int send_battle_setup(SOCKET sock, struct sockaddr_in *peer, const char *pokemonName,
                      int sa_uses, int sd_uses, const char *mode)
{
    char buffer[512];

    snprintf(buffer, sizeof(buffer),
             "message_type: BATTLE_SETUP\n"
             "communication_mode: %s\n"
             "pokemon_name: %s\n"
             "stat_boosts: {\"special_attack_uses\": %d, \"special_defense_uses\": %d}\n",
             mode ? mode : "BROADCAST", pokemonName ? pokemonName : "Unknown", sa_uses, sd_uses);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;

    return send_udp(sock, buffer, dest) >= 0;
}

int receive_battle_setup(SOCKET sock, struct sockaddr_in *peer, char *pokemonName,
                         int *sa_uses, int *sd_uses, char *mode)
{
    char buffer[1024];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* Early GAME_OVER detection */
    char w[50] = {0}, l[50] = {0};
    int seq = 0;
    if (detect_game_over(buffer, w, l, &seq))
    {
        printf("\nGAME OVER signal received during BATTLE_SETUP.\n");
        printf("Winner: %s | Loser: %s\n", w, l);
        fflush(stdout);
        exit(0);
    }

    /* Parse fields safely */
    char *p;
    if ((p = strstr(buffer, "pokemon_name:"))) {
        sscanf(p, "pokemon_name: %49s", pokemonName);
    } else {
        pokemonName[0] = '\0';
    }

    if ((p = strstr(buffer, "communication_mode:"))) {
        sscanf(p, "communication_mode: %9s", mode);
    } else if (mode) {
        strcpy(mode, "BROADCAST");
    }

    int a = 5, d = 5;
    if ((p = strstr(buffer, "stat_boosts:"))) {
        if (sscanf(p,
                   "stat_boosts: {\"special_attack_uses\": %d, \"special_defense_uses\": %d}",
                   &a, &d) != 2)
        {
            a = d = 5;
        }
    }

    if (sa_uses) *sa_uses = a;
    if (sd_uses) *sd_uses = d;

    return 1;
}

/* ---------------------------------------------------------
   ATTACK_ANNOUNCE
--------------------------------------------------------- */
int send_attack_announce(SOCKET sock, struct sockaddr_in *peer, const char *move, int seq_num)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "message_type: ATTACK_ANNOUNCE\n"
             "move_name: %s\n"
             "sequence_number: %d\n",
             move ? move : "tackle", seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

int receive_attack_announce(SOCKET sock, struct sockaddr_in *peer, char *move, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* GAME OVER check */
    char winner[50] = {0}, loser[50] = {0};
    int endseq = 0;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected mid-turn!\nWinner: %s | Loser: %s\n", winner, loser);
        fflush(stdout);
        exit(0);
    }

    char *p;
    if ((p = strstr(buffer, "move_name:"))) {
        sscanf(p, "move_name: %49s", move);
    } else {
        move[0] = '\0';
    }

    if ((p = strstr(buffer, "sequence_number:"))) {
        sscanf(p, "sequence_number: %d", seq_num);
    } else {
        if (seq_num) *seq_num = 0;
    }

    return 1;
}

/* ---------------------------------------------------------
   DEFENSE_ANNOUNCE
--------------------------------------------------------- */
int send_defense_announce(SOCKET sock, struct sockaddr_in *peer, int seq_num)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "message_type: DEFENSE_ANNOUNCE\n"
             "sequence_number: %d\n",
             seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

int recv_defense_announce(SOCKET sock, struct sockaddr_in *peer, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    char winner[50] = {0}, loser[50] = {0};
    int endseq = 0;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected mid-turn!\nWinner: %s | Loser: %s\n", winner, loser);
        fflush(stdout);
        exit(0);
    }

    char *p;
    if ((p = strstr(buffer, "sequence_number:"))) {
        sscanf(p, "sequence_number: %d", seq_num);
    } else {
        if (seq_num) *seq_num = 0;
    }

    return 1;
}

/* ---------------------------------------------------------
   CALCULATION_REPORT
--------------------------------------------------------- */
int send_calculation_report(SOCKET sock, struct sockaddr_in *peer,
                            const char *attacker, const char *move,
                            int remainingHealth, int damageDealt,
                            int defenderRemaining, const char *status, int seq_num)
{
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "message_type: CALCULATION_REPORT\n"
             "attacker: %s\n"
             "move_used: %s\n"
             "remaining_health: %d\n"
             "damage_dealt: %d\n"
             "defender_hp_remaining: %d\n"
             "status_message: %s\n"
             "sequence_number: %d\n",
             attacker ? attacker : "Unknown",
             move ? move : "tackle",
             remainingHealth,
             damageDealt,
             defenderRemaining,
             status ? status : "",
             seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

int recv_calculation_report(SOCKET sock, struct sockaddr_in *peer,
                            char *attacker, char *move, int *remainingHealth,
                            int *damageDealt, int *defenderRemaining,
                            char *status, int *seq_num)
{
    char buffer[1024];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    char winner[50] = {0}, loser[50] = {0};
    int endseq = 0;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected after CALCULATION!\nWinner: %s | Loser: %s\n", winner, loser);
        fflush(stdout);
        exit(0);
    }

    char *p;
    if ((p = strstr(buffer, "attacker:"))) {
        sscanf(p, "attacker: %49s", attacker);
    } else if (attacker) attacker[0] = '\0';

    if ((p = strstr(buffer, "move_used:"))) {
        sscanf(p, "move_used: %49s", move);
    } else if (move) move[0] = '\0';

    if ((p = strstr(buffer, "remaining_health:"))) {
        sscanf(p, "remaining_health: %d", remainingHealth);
    } else if (remainingHealth) *remainingHealth = 0;

    if ((p = strstr(buffer, "damage_dealt:"))) {
        sscanf(p, "damage_dealt: %d", damageDealt);
    } else if (damageDealt) *damageDealt = 0;

    if ((p = strstr(buffer, "defender_hp_remaining:"))) {
        sscanf(p, "defender_hp_remaining: %d", defenderRemaining);
    } else if (defenderRemaining) *defenderRemaining = 0;

    if ((p = strstr(buffer, "status_message:"))) {
        sscanf(p, "status_message: %255[^\n]", status);
    } else if (status) status[0] = '\0';

    if ((p = strstr(buffer, "sequence_number:"))) {
        sscanf(p, "sequence_number: %d", seq_num);
    } else if (seq_num) *seq_num = 0;

    return 1;
}

/* ---------------------------------------------------------
   CALCULATION_CONFIRM
--------------------------------------------------------- */
int send_calculation_confirm(SOCKET sock, struct sockaddr_in *peer, int seq_num)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "message_type: CALCULATION_CONFIRM\n"
             "sequence_number: %d\n",
             seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

int recv_calculation_confirm(SOCKET sock, struct sockaddr_in *peer, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    char winner[50] = {0}, loser[50] = {0};
    int endseq = 0;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected in CONFIRM!\nWinner: %s | Loser: %s\n", winner, loser);
        fflush(stdout);
        exit(0);
    }

    char *p;
    if ((p = strstr(buffer, "sequence_number:"))) {
        sscanf(p, "sequence_number: %d", seq_num);
    } else if (seq_num) *seq_num = 0;

    return 1;
}

/* ---------------------------------------------------------
   RESOLUTION_REQUEST (send only implemented; recv stub)
--------------------------------------------------------- */
int send_resolution_request(SOCKET sock, struct sockaddr_in *peer,
                            const char *attacker, const char *move_used,
                            int damageDealt, int defender_hp_remaining, int seq_num)
{
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "message_type: RESOLUTION_REQUEST\n"
             "attacker: %s\n"
             "move_used: %s\n"
             "damage_dealt: %d\n"
             "defender_hp_remaining: %d\n"
             "sequence_number: %d\n",
             attacker ? attacker : "Unknown",
             move_used ? move_used : "tackle",
             damageDealt, defender_hp_remaining, seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

/* recv_resolution_request is not used by battle engine in provided code.
   Provide a defensive implementation returning 0 if nothing received. */
int recv_resolution_request(SOCKET sock, struct sockaddr_in *peer,
                            char *attacker, char *move_used, int *damageDealt,
                            int *defender_hp_remaining, int *seq_num)
{
    char buffer[512];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer)-1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    char *p;
    if ((p = strstr(buffer, "attacker:"))) {
        sscanf(p, "attacker: %49s", attacker);
    }
    if ((p = strstr(buffer, "move_used:"))) {
        sscanf(p, "move_used: %49s", move_used);
    }
    if ((p = strstr(buffer, "damage_dealt:"))) {
        sscanf(p, "damage_dealt: %d", damageDealt);
    }
    if ((p = strstr(buffer, "defender_hp_remaining:"))) {
        sscanf(p, "defender_hp_remaining: %d", defender_hp_remaining);
    }
    if ((p = strstr(buffer, "sequence_number:"))) {
        sscanf(p, "sequence_number: %d", seq_num);
    }

    return 1;
}

/* ---------------------------------------------------------
   GAME_OVER
--------------------------------------------------------- */
int send_game_over(SOCKET sock, struct sockaddr_in *peer,
                   const char *winner, const char *loser, int seq_num)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "message_type: GAME_OVER\n"
             "winner: %s\n"
             "loser: %s\n"
             "sequence_number: %d\n",
             winner ? winner : "Unknown", loser ? loser : "Unknown", seq_num);

    struct sockaddr_in *dest = peer;
    if (g_use_broadcast) dest = &g_broadcast_addr;
    return send_udp(sock, buffer, dest) >= 0;
}

int receive_game_over(SOCKET sock, struct sockaddr_in *peer,
                      char *winner, char *loser, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp_reliable(sock, buffer, sizeof(buffer) - 1, peer, &from_len, RECV_MAX_RETRIES);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';
    return detect_game_over(buffer, winner, loser, seq_num);
}
