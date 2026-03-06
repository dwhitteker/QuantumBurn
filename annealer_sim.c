#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <math.h>
#include <stdint.h>

#define NUM_NODES 1000
#define ANNEAL_STEPS 10000000  // 10 Million steps per thread
#define NUM_SHOTS 64           // 64 parallel quantum reads

// The massive 1,000 x 1,000 graph of conflicting weights
int8_t J_matrix[NUM_NODES][NUM_NODES];

// Global tracker for the absolute lowest energy found
int global_best_energy = 999999999;
int winning_thread = -1;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

// Calculate the total energy of the 1000-node graph
int calculate_total_energy(int8_t* spins) {
    int total_energy = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            total_energy += -J_matrix[i][j] * spins[i] * spins[j];
        }
    }
    return total_energy;
}

int main() {
    omp_set_dynamic(0);
    
    printf("--- D-WAVE SIMULATOR: SPIN GLASS ANNEALING ---\n");
    printf("Graph Size : %d Nodes | %d Edges\n", NUM_NODES, (NUM_NODES * (NUM_NODES - 1)) / 2);
    printf("Parallel Shots : %d Hardware Threads\n\n", NUM_SHOTS);

    // 1. GENERATE THE NP-HARD PROBLEM LANDSCAPE
    // We create a fully connected graph with random conflicting weights (-1, 0, or 1)
    uint32_t graph_seed = 42; 
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            int weight = (xorshift32(&graph_seed) % 3) - 1; 
            J_matrix[i][j] = weight;
            J_matrix[j][i] = weight; // Symmetric matrix
        }
        J_matrix[i][i] = 0; // No self-loops
    }

    double start_time = omp_get_wtime();

    // 2. THE QUANTUM ANNEALING SHOTS
    #pragma omp parallel num_threads(NUM_SHOTS) proc_bind(spread)
    {
        int thread_id = omp_get_thread_num();
        uint32_t seed = (thread_id + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);
        
        // Thread-local spin array
        int8_t local_spins[NUM_NODES];
        
        // Randomize initial state
        for (int i = 0; i < NUM_NODES; i++) {
            local_spins[i] = (xorshift32(&seed) % 2 == 0) ? 1 : -1;
        }

        int current_energy = calculate_total_energy(local_spins);
        int local_best_energy = current_energy;

        // Annealing Schedule Parameters
        float initial_temp = 100.0f;
        float final_temp = 0.01f;
        float cooling_rate = powf(final_temp / initial_temp, 1.0f / ANNEAL_STEPS);
        float current_temp = initial_temp;

        // The 10-Million-Step Descent
        for (int step = 0; step < ANNEAL_STEPS; step++) {
            
            // Pick a random node to flip
            int target_node = xorshift32(&seed) % NUM_NODES;
            
            // Calculate the exact energy change (Delta E) if we flip this node
            int delta_e = 0;
            for (int j = 0; j < NUM_NODES; j++) {
                delta_e += 2 * J_matrix[target_node][j] * local_spins[target_node] * local_spins[j];
            }

            // Simulated Quantum Tunneling & Thermal Acceptance
            // If delta_e is negative, the flip is an improvement (downhill).
            // If delta_e is positive, we probabilistically accept it to escape local minimums!
            if (delta_e < 0) {
                local_spins[target_node] *= -1;
                current_energy += delta_e;
            } else {
                float rand_val = (float)xorshift32(&seed) / 4294967295.0f;
                if (rand_val < expf(-delta_e / current_temp)) {
                    local_spins[target_node] *= -1; // We phase through the mountain!
                    current_energy += delta_e;
                }
            }

            // Track the absolute lowest point this specific thread ever reached
            if (current_energy < local_best_energy) {
                local_best_energy = current_energy;
            }

            // Cool the system down
            current_temp *= cooling_rate;
        }

        // Thread sync to update the global scoreboard
        #pragma omp critical
        {
            if (local_best_energy < global_best_energy) {
                global_best_energy = local_best_energy;
                winning_thread = thread_id;
            }
        }
    }

    double end_time = omp_get_wtime();

    printf("========== ANNEALING COMPLETE ==========\n");
    printf("Total States Checked : %llu flips simulated\n", (unsigned long long)NUM_SHOTS * ANNEAL_STEPS);
    printf("Lowest Energy Found  : %d\n", global_best_energy);
    printf("Winning Thread       : Thread %d\n", winning_thread);
    printf("Execution Time       : %.4f seconds\n", end_time - start_time);
    printf("========================================\n");

    return 0;
}
