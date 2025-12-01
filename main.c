#include "protocols.h"
#include "network.h"
#include "battle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

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
        sock = create_udp_socket(7000);
        if (sock == INVALID_SOCKET) {
            printf("Failed to create/bind host socket on port 7000\n");
            WSACleanup();
            return 1;
        }
    } else {
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
    }

    // Perform handshake
    if (!perform_handshake(sock, role, &peer, &seed)) {
        printf("Handshake failed!\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("\nHandshake success! Shared seed: %d\n", seed);

    // ================================
    // HANDLE PLAYERS (HOST/JOINER)
    // ================================
    if (role == HOST || role == JOINER) {

        // ---- SELECT OWN POKEMON ----
        char my_pokemon_name[50];
        int my_sa = 5, my_sd = 5;
        char comm_mode[10] = "P2P";

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
                printf("[SPECTATOR RECEIVE]:\n%s\n", buffer);
            }
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
