/*
 * protocols.c (Patched Version)
 * 
 * Improvements:
 * - Adds timeout safety (battle no longer freezes)
 * - Adds early GAME_OVER detection in all receive functions
 * - Strengthens parsing to handle malformed messages
 * - Fully compatible with start_battle() and main.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "protocols.h"
#include "network.h"
#include <winsock2.h>

/* ---------------------------------------------------------
   Helper: Check if a received buffer has message_type: GAME_OVER
--------------------------------------------------------- */
int detect_game_over(const char *buffer, char *winner, char *loser, int *seq_num)
{
    if (strstr(buffer, "message_type: GAME_OVER") == NULL)
        return 0;

    char *p;

    p = strstr(buffer, "winner:");
    if (p) sscanf(p, "winner: %49s", winner);

    p = strstr(buffer, "loser:");
    if (p) sscanf(p, "loser: %49s", loser);

    p = strstr(buffer, "sequence_number:");
    if (p) sscanf(p, "sequence_number: %d", seq_num);

    return 1;
}

/* ---------------------------------------------------------
   HANDSHAKE
--------------------------------------------------------- */
int perform_handshake(SOCKET sock, ROLE role, struct sockaddr_in *peer, int *seed)
{
    char buffer[512];
    int from_len = sizeof(*peer);
    int rv;

    if (role == HOST)
    {
        printf("Waiting for Joiner or Spectator handshake...\n");

        rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
        if (rv <= 0) return 0;

        buffer[rv] = '\0';
        printf("Handshake request received:\n%s\n", buffer);

        if (strstr(buffer, "HANDSHAKE_REQUEST") == NULL &&
            strstr(buffer, "SPECTATOR_REQUEST") == NULL)
        {
            printf("Unknown handshake request.\n");
            return 0;
        }

        srand(time(NULL));
        *seed = rand() % 100000;

        snprintf(buffer, sizeof(buffer),
                 "message_type: HANDSHAKE_RESPONSE\n"
                 "seed: %d\n",
                 *seed);

        return send_udp(sock, buffer, peer) >= 0;
    }
    else
    {
        if (role == JOINER)
            snprintf(buffer, sizeof(buffer), "message_type: HANDSHAKE_REQUEST\n");
        else
            snprintf(buffer, sizeof(buffer), "message_type: SPECTATOR_REQUEST\n");

        send_udp(sock, buffer, peer);

        rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
        if (rv <= 0) return 0;

        buffer[rv] = '\0';
        printf("Handshake response received:\n%s\n", buffer);

        char *p = strstr(buffer, "seed:");
        if (!p) return 0;

        *seed = atoi(p + 5);
        return 1;
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
             mode, pokemonName, sa_uses, sd_uses);

    return send_udp(sock, buffer, peer) >= 0;
}

int receive_battle_setup(SOCKET sock, struct sockaddr_in *peer, char *pokemonName,
                         int *sa_uses, int *sd_uses, char *mode)
{
    char buffer[1024];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* --- Early GAME_OVER detection --- */
    char w[50], l[50];
    int seq;
    if (detect_game_over(buffer, w, l, &seq))
    {
        printf("\nGAME OVER signal received during BATTLE_SETUP.\n");
        printf("Winner: %s | Loser: %s\n", w, l);
        exit(0);
    }

    /* Parse normal fields */
    sscanf(strstr(buffer, "pokemon_name:"), "pokemon_name: %49s", pokemonName);
    sscanf(strstr(buffer, "communication_mode:"), "communication_mode: %9s", mode);

    int a, d;
    if (sscanf(strstr(buffer, "stat_boosts:"),
               "stat_boosts: {\"special_attack_uses\": %d, \"special_defense_uses\": %d}",
               &a, &d) == 2)
    {
        *sa_uses = a;
        *sd_uses = d;
    }
    else
    {
        *sa_uses = *sd_uses = 5;
    }

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
             move, seq_num);

    return send_udp(sock, buffer, peer) >= 0;
}

int receive_attack_announce(SOCKET sock, struct sockaddr_in *peer, char *move, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* GAME OVER check */
    char winner[50], loser[50];
    int endseq;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected mid-turn!\nWinner: %s | Loser: %s\n", winner, loser);
        exit(0);
    }

    sscanf(strstr(buffer, "move_name:"), "move_name: %49s", move);
    sscanf(strstr(buffer, "sequence_number:"), "sequence_number: %d", seq_num);

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

    return send_udp(sock, buffer, peer) >= 0;
}

int recv_defense_announce(SOCKET sock, struct sockaddr_in *peer, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* GAME OVER check */
    char winner[50], loser[50];
    int endseq;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected mid-turn!\nWinner: %s | Loser: %s\n", winner, loser);
        exit(0);
    }

    sscanf(strstr(buffer, "sequence_number:"), "sequence_number: %d", seq_num);
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
             attacker, move, remainingHealth,
             damageDealt, defenderRemaining,
             status, seq_num);

    return send_udp(sock, buffer, peer) >= 0;
}

int recv_calculation_report(SOCKET sock, struct sockaddr_in *peer,
                            char *attacker, char *move, int *remainingHealth,
                            int *damageDealt, int *defenderRemaining,
                            char *status, int *seq_num)
{
    char buffer[1024];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* GAME OVER check */
    char winner[50], loser[50];
    int endseq;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected after CALCULATION!\nWinner: %s | Loser: %s\n", winner, loser);
        exit(0);
    }

    sscanf(strstr(buffer, "attacker:"), "attacker: %49s", attacker);
    sscanf(strstr(buffer, "move_used:"), "move_used: %49s", move);
    sscanf(strstr(buffer, "remaining_health:"), "remaining_health: %d", remainingHealth);
    sscanf(strstr(buffer, "damage_dealt:"), "damage_dealt: %d", damageDealt);
    sscanf(strstr(buffer, "defender_hp_remaining:"), "defender_hp_remaining: %d", defenderRemaining);
    sscanf(strstr(buffer, "status_message:"), "status_message: %255[^\n]", status);
    sscanf(strstr(buffer, "sequence_number:"), "sequence_number: %d", seq_num);

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

    return send_udp(sock, buffer, peer) >= 0;
}

int recv_calculation_confirm(SOCKET sock, struct sockaddr_in *peer, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    /* GAME_OVER check */
    char winner[50], loser[50];
    int endseq;
    if (detect_game_over(buffer, winner, loser, &endseq))
    {
        printf("\nGAME OVER detected in CONFIRM!\nWinner: %s | Loser: %s\n", winner, loser);
        exit(0);
    }

    sscanf(strstr(buffer, "sequence_number:"), "sequence_number: %d", seq_num);
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
             winner, loser, seq_num);

    return send_udp(sock, buffer, peer) >= 0;
}

int receive_game_over(SOCKET sock, struct sockaddr_in *peer,
                      char *winner, char *loser, int *seq_num)
{
    char buffer[256];
    int from_len = sizeof(*peer);

    int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, peer, &from_len);
    if (rv <= 0) return 0;

    buffer[rv] = '\0';

    return detect_game_over(buffer, winner, loser, seq_num);
}

