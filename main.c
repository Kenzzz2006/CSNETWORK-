#include "protocols.h"
#include "network.h"
#include "battle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

// ---- SPECTATOR HELPERS ----
static int spectator_seq = 1000;

int next_spectator_seq(void) {
    return spectator_seq++;
}

int extract_sequence_number(const char* buffer) {
    const char* p = strstr(buffer, "sequence_number:");
    if (!p) p = strstr(buffer, "sequencenumber");
    if (!p) return 0;

    int seq = 0;
    sscanf(p, "%*[^0-9]%d", &seq);
    return seq;
}

void send_ack(SOCKET sock, struct sockaddr_in* peer, int seqnum) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "message_type: ACK\nack_number: %d\n",
             seqnum);
    // IMPORTANT: pass peer, not *peer
    send_udp(sock, buf, peer);
}

void send_chat_message(SOCKET sock,
                       struct sockaddr_in* peer,
                       const char* sender,
                       const char* content_type,
                       const char* text,
                       const char* sticker_data,
                       int seqnum) {
    char buf[2048];

    if (strcmp(content_type, "TEXT") == 0) {
        while (text && (*text == '\n' || *text == '\r')) text++;
        snprintf(buf, sizeof(buf),
            "message_type: CHAT_MESSAGE\n"
            "sender_name: %s\n"
            "content_type: TEXT\n"
            "message_text: %s\n"
            "sequence_number: %d\n",
            sender, text ? text : "", seqnum);
    } else {
        while (sticker_data && (*sticker_data == '\n' || *sticker_data == '\r')) sticker_data++;
        snprintf(buf, sizeof(buf),
            "message_type: CHAT_MESSAGE\n"
            "sender_name: %s\n"
            "content_type: STICKER\n"
            "sticker_data: %s\n"
            "sequence_number: %d\n",
            sender, sticker_data ? sticker_data : "", seqnum);
    }

    send_udp(sock, buf, peer);
}

void show_spectator_message(const char* buffer) {
    if (strstr(buffer, "message_type: HANDSHAKE_RESPONSE")) {
        printf("[NET] Handshake response\n");
    } else if (strstr(buffer, "message_type: BATTLE_SETUP")) {
        char name[50] = {0}, mode[16] = {0};
        int sa = 0, sd = 0;
        char* p;

        p = strstr(buffer, "pokemon_name:");
        if (p) sscanf(p, "pokemon_name: %49[^\n]", name);

        p = strstr(buffer, "communication_mode:");
        if (p) sscanf(p, "communication_mode: %15s", mode);

        p = strstr(buffer, "stat_boosts:");
        if (p) sscanf(p, "stat_boosts: { \"special_attack_uses\": %d, \"special_defense_uses\": %d", &sa, &sd);

        printf("[SETUP] Pokemon=%s mode=%s SA=%d SD=%d\n", name, mode, sa, sd);
    } else if (strstr(buffer, "message_type: ATTACK_ANNOUNCE")) {
        char move[64] = {0};
        char* p = strstr(buffer, "move_name:");
        if (p) sscanf(p, "move_name: %63[^\n]", move);
        int seq = extract_sequence_number(buffer);
        printf("[TURN] Attack announced: %s (seq=%d)\n", move, seq);
    } else if (strstr(buffer, "message_type: DEFENSE_ANNOUNCE")) {
        int seq = extract_sequence_number(buffer);
        printf("[TURN] Defense announce (seq=%d)\n", seq);
    } else if (strstr(buffer, "message_type: CALCULATION_REPORT")) {
        char attacker[50] = {0}, move[50] = {0}, status[256] = {0};
        int dmg = 0, defhp = 0;
        char* p;

        p = strstr(buffer, "attacker:");
        if (p) sscanf(p, "attacker: %49[^\n]", attacker);

        p = strstr(buffer, "move_used:");
        if (p) sscanf(p, "move_used: %49[^\n]", move);

        p = strstr(buffer, "damage_dealt:");
        if (p) sscanf(p, "damage_dealt: %d", &dmg);

        p = strstr(buffer, "defender_hp_remaining:");
        if (p) sscanf(p, "defender_hp_remaining: %d", &defhp);

        p = strstr(buffer, "status_message:");
        if (p) sscanf(p, "status_message: %255[^\n]", status);

        printf("[CALC] %s used %s, damage=%d, defender HP=%d\n",
               attacker, move, dmg, defhp);
        if (status[0]) printf("[TEXT] %s\n", status);
    } else if (strstr(buffer, "message_type: CALCULATION_CONFIRM")) {
        int seq = extract_sequence_number(buffer);
        printf("[CALC] Turn confirmed (seq=%d)\n", seq);
    } else if (strstr(buffer, "message_type: RESOLUTION_REQUEST")) {
        printf("[WARN] Resolution requested by a player.\n");
    } else if (strstr(buffer, "message_type: CHAT_MESSAGE")) {
        char sender[50] = {0}, ctype[16] = {0};
        char* p;

        p = strstr(buffer, "sender_name:");
        if (p) sscanf(p, "sender_name: %49[^\n]", sender);

        p = strstr(buffer, "content_type:");
        if (p) sscanf(p, "content_type: %15s", ctype);

        if (strstr(ctype, "TEXT")) {
            char msg[512] = {0};
            p = strstr(buffer, "message_text:");
            if (p) sscanf(p, "message_text: %511[^\n]", msg);
            printf("[CHAT] %s: %s\n", sender, msg);
        } else {
            printf("[CHAT] %s sent a STICKER\n", sender);
        }
    } else if (strstr(buffer, "message_type: GAME_OVER")) {
        char winner[50] = {0}, loser[50] = {0};
        char* p;

        p = strstr(buffer, "winner:");
        if (p) sscanf(p, "winner: %49[^\n]", winner);

        p = strstr(buffer, "loser:");
        if (p) sscanf(p, "loser: %49[^\n]", loser);

        printf("[GAME_OVER] Winner=%s, Loser=%s\n", winner, loser);
    } else {
        printf("[RAW] %s\n", buffer);
    }
}


int find_pokemon_by_name(Pokemon *pokedex, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(pokedex[i].name, name) == 0)
            return i;
    }
    return -1;
}

int main() {
    printf("Program started!\n");
    fflush(stdout);

    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in peer;
    ROLE role;
    int seed;

    printf("About to load pokedex...\n");
    fflush(stdout);

    Pokemon pokedex[500];
    int pokedex_count = load_pokedex("pokemon.csv", pokedex);
    printf("Pokedex loaded: %d Pokémon\n", pokedex_count);
    fflush(stdout);
    
    if (pokedex_count == 0) {
        printf("Failed to load pokedex.\n");
        return 1;
    }

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Winsock initialization failed!\n");
        return 1;
    }

    // Ask for role
    int choice;
    printf("Select role: 0=HOST, 1=JOINER, 2=SPECTATOR: ");
    fflush(stdout);
    scanf("%d", &choice);
    role = (ROLE)choice;

    // Create socket
    if (role == HOST) {
        // Host must bind to 7000 to receive Joiner and Spectator packets and send broadcasts
        sock = create_udp_socket(7000);
        if (sock == INVALID_SOCKET) {
            printf("Failed to create/bind host socket on port 7000\n");
            WSACleanup();
            return 1;
        }
    } else if (role == JOINER) {
        sock = create_udp_socket(0);
        if (sock == INVALID_SOCKET) {
            printf("Failed to create local socket\n");
            WSACleanup();
            return 1;
        }

        char host_ip[64];
        printf("Enter host IP: ");
        fflush(stdout);
        scanf("%s", host_ip);

        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_port = htons(7000);
        peer.sin_addr.s_addr = inet_addr(host_ip);
    } else { // SPECTATOR
        // Spectator binds to 7000 to receive broadcast battle messages
        sock = create_udp_socket(7000);
        if (sock == INVALID_SOCKET) {
            printf("Failed to create/bind spectator socket on port 7000\n");
            WSACleanup();
            return 1;
        }

        char host_ip[64];
        printf("Enter host IP (for handshake only): ");
        fflush(stdout);
        scanf("%s", host_ip);

        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_port = htons(7000);
        peer.sin_addr.s_addr = inet_addr(host_ip);
    }


    // Perform handshake
    if (role == HOST || role == JOINER) {
        // Perform handshake only between host and joiner
        if (!perform_handshake(sock, role, &peer, &seed)) {
            printf("Handshake failed!\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }
        printf("\nHandshake success! Shared seed: %d\n", seed);
    } else { // SPECTATOR
        // Spectator just listens; no handshake (cannot rely on host listening anymore)
        seed = 0; // not used by spectator
    }


    // ================================
    // HANDLE PLAYERS (HOST/JOINER)
    // ================================
    if (role == HOST || role == JOINER) {

        // ---- SELECT OWN POKEMON ----
        char my_pokemon_name[50];
        int my_sa = 5, my_sd = 5;
        char comm_mode[10] = "BROADCAST";

        printf("\nAvailable Pokémon:\n");
        for(int i = 0; i < pokedex_count; i++){
            printf("%d: %s (%s/%s)\n",
                i + 1,
                pokedex[i].name,
                pokedex[i].type1,
                pokedex[i].type2);
        }

        int poke_choice;
        printf("\nChoose a Pokémon by number: ");
        scanf("%d", &poke_choice);

        if (poke_choice < 1 || poke_choice > pokedex_count) {
            printf("Invalid choice!\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        strcpy(my_pokemon_name, pokedex[poke_choice - 1].name);

        printf("Special Attack uses (default 5): ");
        scanf("%d", &my_sa);

        printf("Special Defense uses (default 5): ");
        scanf("%d", &my_sd);

        // ---- SEND BATTLE_SETUP ----
        send_battle_setup(sock, &peer, my_pokemon_name, my_sa, my_sd, comm_mode);

        // ---- RECEIVE OPPONENT SETUP ----
        char opp_name[50], opp_mode[10];
        int opp_sa, opp_sd;

        if (!receive_battle_setup(sock, &peer, opp_name, &opp_sa, &opp_sd, opp_mode)) {
            printf("Failed to receive opponent's setup!\n");
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        printf("\nOpponent chose: %s\n", opp_name);
        printf("Opponent SA uses: %d, SD uses: %d\n", opp_sa, opp_sd);
        printf("Communication mode: %s\n\n", opp_mode);

        extern int g_use_broadcast;
        extern struct sockaddr_in g_broadcast_addr;

        g_use_broadcast = 0;
        if (strcmp(comm_mode, "BROADCAST") == 0 && strcmp(opp_mode, "BROADCAST") == 0) {
            g_use_broadcast = 1;

            memset(&g_broadcast_addr, 0, sizeof(g_broadcast_addr));
            g_broadcast_addr.sin_family = AF_INET;
            g_broadcast_addr.sin_port   = htons(7000);
            g_broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
        }


        // ---- LOOK UP OPPONENT POKEMON ----
        int opp_index = find_pokemon_by_name(pokedex, pokedex_count, opp_name);
        if (opp_index < 0) {
            printf("ERROR: Opponent Pokémon %s not found in pokedex.\n", opp_name);
            return 1;
        }

        Pokemon myPoke = pokedex[poke_choice - 1];
        Pokemon oppPoke = pokedex[opp_index];

        // ============================
        // CALL THE REAL 5.2 ENGINE
        // ============================
        printf("\nStarting battle engine...\n\n");
        start_battle(
            sock,
            role,
            &peer,
            seed,
            myPoke,
            oppPoke,
            my_sa,
            my_sd,
            opp_sa,
            opp_sd
        );
    }

    // ================================
    // SPECTATOR MODE (not implemented in battle loop yet)
    // ================================
    else if (role == SPECTATOR) {
    printf("Spectator mode connected. Listening to battle...\n");
    printf("(You can receive battle messages but cannot play.)\n");

    char buffer[1024];
    int from_len = sizeof(peer);

    while (1) {
        int rv = recv_udp(sock, buffer, sizeof(buffer) - 1, &peer, &from_len);
        if (rv > 0) {
            buffer[rv] = '\0';

            // Pretty-print the message
            show_spectator_message(buffer);

            // ACK any message that has a sequence_number
            int seq = extract_sequence_number(buffer);
            if (seq > 0) {
                send_ack(sock, &peer, seq);
            }

            // Optional: stop when GAME_OVER is seen
            if (strstr(buffer, "message_type: GAME_OVER")) {
                break;
            }
        }
    }
}


    closesocket(sock);
    WSACleanup();
    return 0;
}