#ifndef BATTLE_H
#define BATTLE_H

#include <winsock2.h>
#include "protocols.h"
#include "network.h"

typedef struct {
    char name[50];
    char type1[20];
    char type2[20];
    int hp;
    int attack;
    int defense;
    int sp_attack;
    int sp_defense;
    int speed;
    float against_bug;
    float against_dark;
    float against_dragon;
    float against_electric;
    float against_fairy;
    float against_fight;
    float against_fire;
    float against_flying;
    float against_ghost;
    float against_grass;
    float against_ground;
    float against_ice;
    float against_normal;
    float against_poison;
    float against_psychic;
    float against_rock;
    float against_steel;
    float against_water;
} Pokemon;

/* Main battle loop for section 5.2 */
void start_battle(
    SOCKET sock,
    ROLE role,
    struct sockaddr_in *peer,
    int seed,
    Pokemon myPokemon,
    Pokemon oppPokemon,
    int my_sa,
    int my_sd,
    int opp_sa,
    int opp_sd
);

/* Corrected damage function */
int calculate_damage(int basePower, int attackerStat, float typeMultiplier, int defenderStat);

/* Load Pokémon data from CSV */
int load_pokedex(const char *filename, Pokemon pokedex[]);

/* Special attack / defense boost */
void use_stat_boost(int *boost_count);

/* NEW — used by 5.2 damage phase */
float get_type_multiplier(Pokemon defender, const char *move_type);

/* NEW — temporary move-type mapping */
void get_move_type(const char *move_name, char *move_type_out);

#endif
