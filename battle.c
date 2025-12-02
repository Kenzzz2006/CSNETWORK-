#include "battle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static int g_current_seq = 1;
static int g_turn_number = 0;

/* 5.2 Helper: Compare two calculation reports for equality */
int calculation_reports_equal(
    const char *attacker1, const char *move1, int remainingHealth1, 
    int damageDealt1, int defenderHP1, const char *status1,
    const char *attacker2, const char *move2, int remainingHealth2, 
    int damageDealt2, int defenderHP2, const char *status2
) {
    return strcmp(attacker1, attacker2) == 0 &&
           strcmp(move1, move2) == 0 &&
           remainingHealth1 == remainingHealth2 &&
           damageDealt1 == damageDealt2 &&
           defenderHP1 == defenderHP2 &&
           strcmp(status1, status2) == 0;
}

/* 5.2 Helper: Perform damage calculation (deterministic with seed) */
int perform_damage_calculation(Pokemon *attacker, Pokemon *defender, 
                              const char *move_name, int *damage, 
                              int *remaining_attacker_hp, int *remaining_defender_hp,
                              char *status_message, int seed, int turn_number) {
    
    srand(seed + turn_number); // Deterministic RNG based on seed + turn
    
    // Simplified damage formula (you can expand this with type effectiveness, etc.)
    int base_power = 40; // Default move power
    double modifier = ((double)rand() / RAND_MAX) * 0.85 + 0.85; // Random factor 0.85-1.7
    
    int attacker_stat = (strcmp("special", "special") == 0) ? 
                       attacker->special_attack : attacker->attack;
    int defender_stat = (strcmp("special", "special") == 0) ? 
                       defender->special_defense : defender->defense;
    
    *damage = (int)((((2 * 100 / 5 + 2) * base_power * attacker_stat / defender_stat / 50) + 2) * modifier);
    
    // Ensure damage doesn't exceed defender's HP
    *damage = (*damage > defender->hp) ? defender->hp : *damage;
    
    *remaining_attacker_hp = attacker->hp;
    *remaining_defender_hp = defender->hp - *damage;
    
    if (*remaining_defender_hp <= 0) {
        strcpy(status_message, "Foe's Pokemon fainted!");
        return 1; // Game over
    } else if (*damage >= defender->hp * 0.8) {
        strcpy(status_message, "It was super effective!");
    } else if (*damage <= defender->hp * 0.2) {
        strcpy(status_message, "It was not very effective...");
    } else {
        snprintf(status_message, 256, "%s used %s!", attacker->name, move_name);
    }
    
    return 0; // Continue battle
}

/* 5.2 Helper: Handle resolution discrepancy */
int handle_resolution_discrepancy(SOCKET sock, struct sockaddr_in *peer,
                                 const char *my_attacker, const char *my_move,
                                 int my_damage, int my_defender_hp, int seq_num,
                                 const char *opp_attacker, const char *opp_move,
                                 int opp_damage, int opp_defender_hp) {
    
    printf("\n[DISCREPANCY] Calculation mismatch detected!\n");
    printf("My calc: %s used %s, damage=%d, defender HP=%d\n", 
           my_attacker, my_move, my_damage, my_defender_hp);
    printf("Opp calc: %s used %s, damage=%d, defender HP=%d\n", 
           opp_attacker, opp_move, opp_damage, opp_defender_hp);
    
    // Simple resolution: take the average, or use majority vote, or trust the host
    // For now, we'll just use the opponent's values (you can make this more sophisticated)
    if (my_damage != opp_damage || my_defender_hp != opp_defender_hp) {
        printf("[RESOLUTION] Using opponent's calculation values.\n");
        
        // Send ACK to opponent's RESOLUTION_REQUEST
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
                 "message_type: ACK\nack_number: %d\n", seq_num);
        send_udp(sock, buffer, peer);
        
        return 1; // Resolved with opponent's values
    }
    
    // If still can't agree, terminate battle
    printf("[ERROR] Cannot resolve discrepancy. Battle terminated.\n");
    send_game_over(sock, peer, "ERROR", "ERROR", g_current_seq++);
    return 0; // Terminate
}

/* 5.2 Helper: Update battle state after successful turn */
void battle_state_update(Pokemon *my_poke, Pokemon *opp_poke,
                        int damage, int remaining_health, int defender_hp_remaining) {
    // Update defender's HP
    if (defender_hp_remaining > 0) {
        opp_poke->hp = defender_hp_remaining; // Assuming opponent is defender
    } else {
        opp_poke->hp = 0;
    }
    
    // Print battle state
    printf("\n--- BATTLE STATUS ---\n");
    printf("%s HP: %d/%d\n", my_poke->name, remaining_health, my_poke->hp);
    printf("%s HP: %d/%d\n", opp_poke->name, defender_hp_remaining, opp_poke->hp);
    printf("--------------------\n\n");
}

/* Load pokedex from CSV (your existing implementation) */
int load_pokedex(const char *filename, Pokemon *pokedex) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Cannot open %s\n", filename);
        return 0;
    }
    
    // Skip header line
    char line[512];
    fgets(line, sizeof(line), file);
    
    int count = 0;
    while (fgets(line, sizeof(line), file) && count < 500) {
        // Simplified CSV parsing - adjust based on your CSV format
        int fields = sscanf(line, "%49[^,],%19[^,],%19[^,],%d,%d,%d,%d,%d,%d",
                           pokedex[count].name,
                           pokedex[count].type1,
                           pokedex[count].type2,
                           &pokedex[count].hp,
                           &pokedex[count].attack,
                           &pokedex[count].defense,
                           &pokedex[count].special_attack,
                           &pokedex[count].special_defense,
                           &pokedex[count].speed);
        
        if (fields >= 9) {
            // Set default moves
            strcpy(pokedex[count].moves[0], "Tackle");
            strcpy(pokedex[count].moves[1], "Growl");
            strcpy(pokedex[count].moves[2], "Quick Attack");
            strcpy(pokedex[count].moves[3], "Tail Whip");
            count++;
        }
    }
    
    fclose(file);
    return count;
}

/* 5.2 Complete: Main battle engine implementing full four-step handshake */
void start_battle(SOCKET sock, ROLE role, struct sockaddr_in *peer, int seed,
                  Pokemon my_poke, Pokemon opp_poke,
                  int my_sa_uses, int my_sd_uses, int opp_sa_uses, int opp_sd_uses) {
    
    Pokemon my_pokemon = my_poke;
    Pokemon opponent_pokemon = opp_poke;
    
    int my_turn = (role == HOST); // Host goes first
    int game_over = 0;
    char buffer[1024];
    
    printf("Battle begins! %s vs %s\n", my_pokemon.name, opponent_pokemon.name);
    printf("My turn: %s\n\n", my_turn ? "Yes" : "No");
    
    // Initialize sequence numbers
    g_current_seq = 1;
    g_turn_number = 1;
    
    while (!game_over) {
        char move_name[50];
        char status_message[256];
        int damage, my_remaining_hp, opp_remaining_hp;
        char my_report_attacker[50], my_report_move[50];
        
        if (my_turn) {
            // ===== 5.2 ATTACKER PHASE =====
            printf("\n--- YOUR TURN (Attacker) ---\n");
            
            // Step 1: Announce attack
            printf("Choose a move: ");
            scanf("%s", move_name);
            strcpy(my_report_move, move_name);
            strcpy(my_report_attacker, my_pokemon.name);
            
            printf("Sending ATTACK_ANNOUNCE: %s\n", move_name);
            send_attack_announce(sock, peer, move_name, g_current_seq++);
            
            // Step 2: Wait for defense announce
            int opp_def_seq;
            printf("Waiting for opponent to acknowledge...\n");
            if (!recv_defense_announce(sock, peer, &opp_def_seq)) {
                printf("ERROR: No DEFENSE_ANNOUNCE received!\n");
                return;
            }
            printf("Opponent acknowledged (seq=%d)\n", opp_def_seq);
            
            // Step 3: Both calculate damage
            printf("Calculating damage...\n");
            if (perform_damage_calculation(&my_pokemon, &opponent_pokemon, move_name, 
                                         &damage, &my_remaining_hp, &opp_remaining_hp, 
                                         status_message, seed, g_turn_number)) {
                // Opponent fainted
                printf("%s\n", status_message);
                send_game_over(sock, peer, my_pokemon.name, opponent_pokemon.name, g_current_seq++);
                game_over = 1;
                break;
            }
            
            printf("%s\n", status_message);
            
            // Step 4: Send calculation report
            printf("Sending CALCULATION_REPORT...\n");
            send_calculation_report(sock, peer, my_report_attacker, my_report_move, 
                                  my_remaining_hp, damage, opp_remaining_hp, 
                                  status_message, g_current_seq++);
            
            // Step 5: Receive opponent's calculation report
            char opp_attacker[50], opp_move[50], opp_status[256];
            int opp_remaining_hp, opp_damage, opp_defender_hp, opp_seq;
            
            printf("Waiting for opponent's CALCULATION_REPORT...\n");
            if (!recv_calculation_report(sock, peer, opp_attacker, opp_move, 
                                       &opp_remaining_hp, &opp_damage, &opp_defender_hp, 
                                       opp_status, &opp_seq)) {
                printf("ERROR: No CALCULATION_REPORT from opponent!\n");
                return;
            }
            
            printf("Opponent's report: %s used %s, damage=%d\n", 
                   opp_attacker, opp_move, opp_damage);
            
            // Step 6: Compare calculations
            if (calculation_reports_equal(
                    my_report_attacker, my_report_move, my_remaining_hp, damage, opp_remaining_hp, status_message,
                    opp_attacker, opp_move, opp_remaining_hp, opp_damage, opp_defender_hp, opp_status
                )) {
                
                // Calculations match! Send confirmations
                printf("Calculations match! Confirming...\n");
                send_calculation_confirm(sock, peer, g_current_seq++);
                
                int confirm_seq;
                if (recv_calculation_confirm(sock, peer, &confirm_seq)) {
                    printf("Turn confirmed (seq=%d)\n", confirm_seq);
                    
                    // Update battle state
                    battle_state_update(&my_pokemon, &opponent_pokemon, 
                                      damage, my_remaining_hp, opp_remaining_hp);
                } else {
                    printf("ERROR: No confirmation from opponent!\n");
                    return;
                }
                
            } else {
                // Discrepancy! Request resolution
                printf("Calculation discrepancy detected!\n");
                
                if (!handle_resolution_discrepancy(sock, peer, my_report_attacker, my_report_move,
                                                  damage, opp_remaining_hp,
                                                  g_current_seq - 1, // Previous seq
                                                  opp_attacker, opp_move,
                                                  opp_damage, opp_defender_hp)) {
                    // Could not resolve
                    printf("Battle terminated due to irreconcilable discrepancy.\n");
                    return;
                }
                
                // Update with resolved values (using opponent's for simplicity)
                damage = opp_damage;
                opp_remaining_hp = opp_defender_hp;
                battle_state_update(&my_pokemon, &opponent_pokemon, 
                                  damage, my_remaining_hp, opp_remaining_hp);
            }
            
            // Check if opponent fainted after resolution
            if (opp_remaining_hp <= 0) {
                send_game_over(sock, peer, my_pokemon.name, opponent_pokemon.name, g_current_seq++);
                game_over = 1;
                break;
            }
            
        } else {
            // ===== 5.2 DEFENDER PHASE =====
            printf("\n--- OPPONENT'S TURN (You defend) ---\n");
            
            // Step 1: Receive attack announce
            char opp_move[50];
            int attack_seq;
            printf("Waiting for opponent's attack...\n");
            
            if (!receive_attack_announce(sock, peer, opp_move, &attack_seq)) {
                printf("ERROR: No ATTACK_ANNOUNCE received!\n");
                return;
            }
            
            printf("Opponent announces: %s (seq=%d)\n", opp_move, attack_seq);
            
            // Step 2: Send defense announce
            printf("Acknowledging opponent's attack...\n");
            send_defense_announce(sock, peer, g_current_seq++);
            
            // Step 3: Calculate damage (as defender)
            strcpy(my_report_move, opp_move);
            strcpy(my_report_attacker, opponent_pokemon.name);
            
            if (perform_damage_calculation(&opponent_pokemon, &my_pokemon, opp_move, 
                                         &damage, &opp_remaining_hp, &my_remaining_hp, 
                                         status_message, seed, g_turn_number)) {
                // I fainted
                printf("%s\n", status_message);
                send_game_over(sock, peer, opponent_pokemon.name, my_pokemon.name, g_current_seq++);
                game_over = 1;
                break;
            }
            
            printf("%s\n", status_message);
            
            // Step 4: Send calculation report
            printf("Sending CALCULATION_REPORT (as defender)...\n");
            send_calculation_report(sock, peer, my_report_attacker, my_report_move, 
                                  opp_remaining_hp, damage, my_remaining_hp, 
                                  status_message, g_current_seq++);
            
            // Step 5: Receive opponent's calculation report
            char opp_attacker[50], opp_move_name[50], opp_status[256];
            int opp_remaining_hp_def, opp_damage, opp_my_remaining_hp, opp_seq_def;
            
            printf("Waiting for opponent's CALCULATION_REPORT...\n");
            if (!recv_calculation_report(sock, peer, opp_attacker, opp_move_name, 
                                       &opp_remaining_hp_def, &opp_damage, &opp_my_remaining_hp, 
                                       opp_status, &opp_seq_def)) {
                printf("ERROR: No CALCULATION_REPORT from opponent!\n");
                return;
            }
            
            printf("Opponent's report: %s used %s, damage=%d\n", 
                   opp_attacker, opp_move_name, opp_damage);
            
            // Step 6: Compare calculations
            if (calculation_reports_equal(
                    my_report_attacker, my_report_move, opp_remaining_hp, damage, my_remaining_hp, status_message,
                    opp_attacker, opp_move_name, opp_remaining_hp_def, opp_damage, opp_my_remaining_hp, opp_status
                )) {
                
                // Calculations match! Send confirmations
                printf("Calculations match! Confirming...\n");
                send_calculation_confirm(sock, peer, g_current_seq++);
                
                int confirm_seq;
                if (recv_calculation_confirm(sock, peer, &confirm_seq)) {
                    printf("Turn confirmed (seq=%d)\n", confirm_seq);
                    
                    // Update battle state (I took damage)
                    battle_state_update(&my_pokemon, &opponent_pokemon, 
                                      damage, my_remaining_hp, opp_remaining_hp);
                } else {
                    printf("ERROR: No confirmation from opponent!\n");
                    return;
                }
                
            } else {
                // Discrepancy! Request resolution
                printf("Calculation discrepancy detected!\n");
                
                if (!handle_resolution_discrepancy(sock, peer, my_report_attacker, my_report_move,
                                                  damage, my_remaining_hp,
                                                  g_current_seq - 1,
                                                  opp_attacker, opp_move_name,
                                                  opp_damage, opp_my_remaining_hp)) {
                    // Could not resolve
                    printf("Battle terminated due to irreconcilable discrepancy.\n");
                    return;
                }
                
                // Update with resolved values
                damage = opp_damage;
                my_remaining_hp = opp_my_remaining_hp;
                battle_state_update(&my_pokemon, &opponent_pokemon, 
                                  damage, my_remaining_hp, opp_remaining_hp);
            }
            
            // Check if I fainted after resolution
            if (my_remaining_hp <= 0) {
                send_game_over(sock, peer, opponent_pokemon.name, my_pokemon.name, g_current_seq++);
                game_over = 1;
                break;
            }
        }
        
        // Turn complete, swap turns
        my_turn = !my_turn;
        g_turn_number++;
        
        // Check for GAME_OVER message
        if (receive_game_over(sock, peer, buffer, buffer, NULL)) {
            printf("Battle ended: %s\n", buffer);
            game_over = 1;
            break;
        }
    }
    
    if (game_over) {
        printf("\n=== BATTLE ENDED ===\n");
        if (my_pokemon.hp > 0 && opponent_pokemon.hp <= 0) {
            printf("Victory! %s wins!\n", my_pokemon.name);
        } else if (my_pokemon.hp <= 0 && opponent_pokemon.hp > 0) {
            printf("Defeat! %s wins!\n", opponent_pokemon.name);
        } else {
            printf("Battle terminated unexpectedly.\n");
        }
    }
}

