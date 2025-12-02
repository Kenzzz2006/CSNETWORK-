#ifndef BATTLE_H
#define BATTLE_H

#include "protocols.h"
#include <stdio.h>

typedef struct {
    char name[50];
    char type1[20];
    char type2[20];
    int hp;
    int attack;
    int defense;
    int special_attack;
    int special_defense;
    int speed;
    char moves[4][50]; // 4 moves per Pokemon
} Pokemon;

int load_pokedex(const char *filename, Pokemon *pokedex);
void start_battle(SOCKET sock, ROLE role, struct sockaddr_in *peer, int seed,
                  Pokemon my_poke, Pokemon opp_poke,
                  int my_sa_uses, int my_sd_uses, int opp_sa_uses, int opp_sd_uses);

// 5.2 Helper functions
int calculation_reports_equal(
    const char *attacker1, const char *move1, int remainingHealth1, 
    int damageDealt1, int defenderHP1, const char *status1,
    const char *attacker2, const char *move2, int remainingHealth2, 
    int damageDealt2, int defenderHP2, const char *status2
);

int perform_damage_calculation(Pokemon *attacker, Pokemon *defender, 
                              const char *move_name, int *damage, 
                              int *remaining_attacker_hp, int *remaining_defender_hp,
                              char *status_message, int seed, int turn_number);

int handle_resolution_discrepancy(SOCKET sock, struct sockaddr_in *peer,
                                 const char *my_attacker, const char *my_move,
                                 int my_damage, int my_defender_hp, int seq_num,
                                 const char *opp_attacker, const char *opp_move,
                                 int opp_damage, int opp_defender_hp);

void battle_state_update(Pokemon *my_poke, Pokemon *opp_poke,
                        int damage, int remaining_health, int defender_hp_remaining);

#endif

