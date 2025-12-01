#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include "network.h"

typedef enum { HOST=0, JOINER=1, SPECTATOR=2 } ROLE;

int perform_handshake(SOCKET sock, ROLE role, struct sockaddr_in *peer, int *seed);
int send_battle_setup(SOCKET sock, struct sockaddr_in *peer, const char *pokemonName,
                      int sa_uses, int sd_uses, const char *mode);
int receive_battle_setup(SOCKET sock, struct sockaddr_in *peer, char *pokemonName,
                         int *sa_uses, int *sd_uses, char *mode);    
int send_attack_announce(SOCKET sock, struct sockaddr_in *peer, const char *move_name, int seq_num);
int receive_attack_announce(SOCKET sock, struct sockaddr_in *peer, char *move_name, int *seq_num);
int send_defense_announce(SOCKET sock, struct sockaddr_in *peer, int seq_num);
int recv_defense_announce(SOCKET sock, struct sockaddr_in *peer, int *seq_num);
int send_calculation_report(SOCKET sock, struct sockaddr_in *peer,
                            const char *attacker, const char *move_used,
                            int remainingHealth, int damageDealt, int defender_hp_remaining,
                            const char *status_msg, int seq_num);
int recv_calculation_report(SOCKET sock, struct sockaddr_in *peer,
                            char *attacker, char *move_used, int *remainingHealth,
                            int *damageDealt, int *defender_hp_remaining, char *status_msg, int *seq_num);
int send_calculation_confirm(SOCKET sock, struct sockaddr_in *peer, int seq_num);
int recv_calculation_confirm(SOCKET sock, struct sockaddr_in *peer, int *seq_num);
int send_resolution_request(SOCKET sock, struct sockaddr_in *peer,
                            const char *attacker, const char *move_used,
                            int damageDealt, int defender_hp_remaining, int seq_num);
int recv_resolution_request(SOCKET sock, struct sockaddr_in *peer,
                            char *attacker, char *move_used, int *damageDealt,
                            int *defender_hp_remaining, int *seq_num);
int send_game_over(SOCKET sock, struct sockaddr_in *peer, const char *winner, const char *loser, int seq_num);
int receive_game_over(SOCKET sock, struct sockaddr_in *peer, char *winner, char *loser, int *seq_num);
    

#endif