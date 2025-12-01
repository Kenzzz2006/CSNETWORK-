#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "battle.h"

// ----------------------------
// 6.0 Damage Calculation
// ----------------------------
int calculate_damage(int basePower, int attackerStat, float typeMultiplier, int defenderStat) {
    float dmg = (basePower * attackerStat * typeMultiplier) / defenderStat;
    if (dmg < 1) dmg = 1;  
    return (int)dmg;
}

void use_stat_boost(int *boost_count) {
    if (*boost_count > 0) {
        (*boost_count)--;
    }
}

// ----------------------------
// Load pokedex
// ----------------------------
int load_pokedex(const char *filename, Pokemon pokedex[]) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("ERROR: Could not open %s\n", filename);
        return 0;
    }

    char line[2048];
    int count = 0;

    fgets(line, sizeof(line), file); // skip header

    while (fgets(line, sizeof(line), file) && count < 500) {
        int field = 0;
        char *ptr = line;
        int in_brackets = 0;
        char token[256];
        int token_idx = 0;

        while (*ptr) {
            if (*ptr == '[') in_brackets = 1;
            else if (*ptr == ']') in_brackets = 0;
            else if (*ptr == ',' && !in_brackets) {
                token[token_idx] = '\0';

                switch (field) {
                    case 1:  pokedex[count].against_bug = atof(token); break;
                    case 2:  pokedex[count].against_dark = atof(token); break;
                    case 3:  pokedex[count].against_dragon = atof(token); break;
                    case 4:  pokedex[count].against_electric = atof(token); break;
                    case 5:  pokedex[count].against_fairy = atof(token); break;
                    case 6:  pokedex[count].against_fight = atof(token); break;
                    case 7:  pokedex[count].against_fire = atof(token); break;
                    case 8:  pokedex[count].against_flying = atof(token); break;
                    case 9:  pokedex[count].against_ghost = atof(token); break;
                    case 10: pokedex[count].against_grass = atof(token); break;
                    case 11: pokedex[count].against_ground = atof(token); break;
                    case 12: pokedex[count].against_ice = atof(token); break;
                    case 13: pokedex[count].against_normal = atof(token); break;
                    case 14: pokedex[count].against_poison = atof(token); break;
                    case 15: pokedex[count].against_psychic = atof(token); break;
                    case 16: pokedex[count].against_rock = atof(token); break;
                    case 17: pokedex[count].against_steel = atof(token); break;
                    case 18: pokedex[count].against_water = atof(token); break;
                    case 19: pokedex[count].attack = atoi(token); break;
                    case 25: pokedex[count].defense = atoi(token); break;
                    case 28: pokedex[count].hp = atoi(token); break;
                    case 30: strncpy(pokedex[count].name, token, 49); break;
                    case 33: pokedex[count].sp_attack = atoi(token); break;
                    case 34: pokedex[count].sp_defense = atoi(token); break;
                    case 35: pokedex[count].speed = atoi(token); break;
                    case 36: strncpy(pokedex[count].type1, token, 19); break;
                    case 37: strncpy(pokedex[count].type2, token, 19); break;
                }

                field++;
                token_idx = 0;
                ptr++;
                continue;
            }

            if (*ptr != '\n' && *ptr != '\r') {
                token[token_idx++] = *ptr;
            }
            ptr++;
        }

        // last field (type2)
        if (token_idx > 0) {
            token[token_idx] = '\0';
            if (field == 37)
                strncpy(pokedex[count].type2, token, 19);
        }

        count++;
    }

    fclose(file);
    printf("Successfully loaded %d Pokemon!\n", count);
    return count;
}

// ----------------------------
// Type multipliers
// ----------------------------
float get_type_multiplier(Pokemon defender, const char *move_type) {
    if (strcmp(move_type, "bug") == 0) return defender.against_bug;
    if (strcmp(move_type, "dark") == 0) return defender.against_dark;
    if (strcmp(move_type, "dragon") == 0) return defender.against_dragon;
    if (strcmp(move_type, "electric") == 0) return defender.against_electric;
    if (strcmp(move_type, "fairy") == 0) return defender.against_fairy;
    if (strcmp(move_type, "fight") == 0) return defender.against_fight;
    if (strcmp(move_type, "fire") == 0) return defender.against_fire;
    if (strcmp(move_type, "flying") == 0) return defender.against_flying;
    if (strcmp(move_type, "ghost") == 0) return defender.against_ghost;
    if (strcmp(move_type, "grass") == 0) return defender.against_grass;
    if (strcmp(move_type, "ground") == 0) return defender.against_ground;
    if (strcmp(move_type, "ice") == 0) return defender.against_ice;
    if (strcmp(move_type, "normal") == 0) return defender.against_normal;
    if (strcmp(move_type, "poison") == 0) return defender.against_poison;
    if (strcmp(move_type, "psychic") == 0) return defender.against_psychic;
    if (strcmp(move_type, "rock") == 0) return defender.against_rock;
    if (strcmp(move_type, "steel") == 0) return defender.against_steel;
    if (strcmp(move_type, "water") == 0) return defender.against_water;

    return 1.0f;
}

// ----------------------------
// Temporary move-to-type
// ----------------------------
void get_move_type(const char *move_name, char *move_type_out) {
    if (strcmp(move_name, "tackle") == 0) strcpy(move_type_out, "normal");
    else if (strcmp(move_name, "ember") == 0) strcpy(move_type_out, "fire");
    else if (strcmp(move_name, "watergun") == 0) strcpy(move_type_out, "water");
    else if (strcmp(move_name, "vinewhip") == 0) strcpy(move_type_out, "grass");
    else strcpy(move_type_out, "normal");
}

// ----------------------------
// Start battle (5.2)
// ----------------------------
void start_battle(
    SOCKET sock, ROLE role, struct sockaddr_in *peer, int seed,
    Pokemon myPokemon, Pokemon oppPokemon,
    int my_sa, int my_sd, int opp_sa, int opp_sd)
{
    srand(seed);

    int myTurn = (role == HOST) ? 1 : 0;
    int seq = 1;

    printf("\n--- BATTLE START ---\n");

    while (1) {
        if (myTurn) {
            // -------------------------
            // My turn
            // -------------------------
            char move[50];
            printf("Your turn! Enter a move: ");
            scanf("%s", move);

            send_attack_announce(sock, peer, move, seq++);

            int def_seq;
            recv_defense_announce(sock, peer, &def_seq);

            char move_type[20];
            get_move_type(move, move_type);

            int attackerStat = myPokemon.attack;
            int defenderStat = oppPokemon.defense;

            float typeMult = get_type_multiplier(oppPokemon, move_type);
            int dmg = calculate_damage(40, attackerStat, typeMult, defenderStat);

            oppPokemon.hp -= dmg;
            if (oppPokemon.hp < 0) oppPokemon.hp = 0;

            char status[150];
            snprintf(status, sizeof(status),
                "%s used %s (%s type) dealing %d damage! x%.2f",
                myPokemon.name, move, move_type, dmg, typeMult);

            send_calculation_report(
                sock, peer,
                myPokemon.name, move,
                myPokemon.hp, dmg, oppPokemon.hp,
                status, seq++
            );

            int conf;
            recv_calculation_confirm(sock, peer, &conf);

            if (oppPokemon.hp <= 0) {
                send_game_over(sock, peer, myPokemon.name, oppPokemon.name, seq++);
                printf("Opponent fainted! YOU WIN!\n");
                return;
            }
        }

        else {
            // -------------------------
            // Opponent turn
            // -------------------------
            char move[50];
            int atk_seq;
            receive_attack_announce(sock, peer, move, &atk_seq);

            send_defense_announce(sock, peer, seq++);

            char move_type[20];
            get_move_type(move, move_type);

            int attackerStat = oppPokemon.attack;
            int defenderStat = myPokemon.defense;

            float typeMult = get_type_multiplier(myPokemon, move_type);
            int dmg = calculate_damage(40, attackerStat, typeMult, defenderStat);

            myPokemon.hp -= dmg;
            if (myPokemon.hp < 0) myPokemon.hp = 0;

            char status[150];
            snprintf(status, sizeof(status),
                "%s used %s (%s type) dealing %d damage! x%.2f",
                oppPokemon.name, move, move_type, dmg, typeMult);

            send_calculation_report(
                sock, peer,
                oppPokemon.name, move,
                oppPokemon.hp, dmg, myPokemon.hp,
                status, seq++
            );

            int conf;
            recv_calculation_confirm(sock, peer, &conf);

            if (myPokemon.hp <= 0) {
                send_game_over(sock, peer, oppPokemon.name, myPokemon.name, seq++);
                printf("Your Pokémon fainted! YOU LOSE!\n");
                return;
            }
        }

        printf("\nYour HP: %d | Opponent HP: %d\n",
               myPokemon.hp, oppPokemon.hp);

        myTurn = !myTurn;
    }
}
