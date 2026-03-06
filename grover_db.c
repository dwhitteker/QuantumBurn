#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include <time.h>
#include <stdint.h>

#define HISTORY_SIZE 1048576 

#define GATE_HADAMARD 1
#define GATE_CNOT     2
#define GATE_TOFFOLI  3
#define GATE_PAULI_X  4

typedef struct {
    int gate_type;
    int target_qubit;
    int control_qubit_1;
    int control_qubit_2;
} GateRecord;

GateRecord* qubit_history;
float* amplitudes;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

int dynamic_grover_search(int search_gate, int search_target) {
    // --- 1. SIMULATED QUANTUM COUNTING ---
    // In real hardware, this uses Quantum Phase Estimation.
    // Here, we simulate knowing 'M' (the number of matches).
    int M = 0;
    #pragma omp parallel for reduction(+:M) num_threads(64)
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (qubit_history[i].gate_type == search_gate && 
            qubit_history[i].target_qubit == search_target) {
            M++;
        }
    }

    if (M == 0) return -1; // Not found in database

    // Recalculate the optimal iterations based on M
    int grover_iterations = (int)((M_PI / 4.0) * sqrt((double)HISTORY_SIZE / (double)M));
    
    printf("\n--- QUANTUM COUNTING RESULTS ---\n");
    printf("Matches Found (M): %d clones exist in the database\n", M);
    printf("Tuned Iterations : %d steps (down from 804)\n\n", grover_iterations);

    // --- 2. SUPERPOSITION INITIALIZATION ---
    float initial_amp = 1.0f / sqrt((float)HISTORY_SIZE);
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for (int i = 0; i < HISTORY_SIZE; i++) {
        amplitudes[i] = initial_amp;
    }

    // --- 3. THE QUANTUM LOOP ---
    for (int iter = 0; iter < grover_iterations; iter++) {
        
        // The Oracle
        #pragma omp parallel for num_threads(64) proc_bind(spread)
        for (int i = 0; i < HISTORY_SIZE; i++) {
            if (qubit_history[i].gate_type == search_gate && 
                qubit_history[i].target_qubit == search_target) {
                amplitudes[i] = -amplitudes[i]; 
            }
        }

        // Diffusion
        double global_sum = 0.0;
        #pragma omp parallel for reduction(+:global_sum) num_threads(64)
        for (int i = 0; i < HISTORY_SIZE; i++) {
            global_sum += amplitudes[i];
        }
        
        float average_amp = (float)(global_sum / HISTORY_SIZE);

        #pragma omp parallel for num_threads(64) proc_bind(spread)
        for (int i = 0; i < HISTORY_SIZE; i++) {
            amplitudes[i] = (2.0f * average_amp) - amplitudes[i];
        }
    }

    // --- 4. MEASUREMENT ---
    int measured_index = -1;
    float max_prob = 0.0f;
    
    for (int i = 0; i < HISTORY_SIZE; i++) {
        float prob = amplitudes[i] * amplitudes[i];
        if (prob > max_prob) {
            max_prob = prob;
            measured_index = i;
        }
    }

    return measured_index;
}

int main() {
    omp_set_dynamic(0);
    
    qubit_history = (GateRecord*)aligned_alloc(64, sizeof(GateRecord) * HISTORY_SIZE);
    amplitudes = (float*)aligned_alloc(64, sizeof(float) * HISTORY_SIZE);
    
    uint32_t seed = (uint32_t)time(NULL);

    printf("--- INITIALIZING QUBIT GATE HISTORY (UNCENSORED NOISE) ---\n");
    
    #pragma omp parallel for num_threads(64)
    for (int i = 0; i < HISTORY_SIZE; i++) {
        uint32_t local_seed = seed + i;
        
        // No filters! The randomizer will naturally create ~350 clones.
        qubit_history[i].gate_type = (xorshift32(&local_seed) % 3) + 1; 
        qubit_history[i].target_qubit = xorshift32(&local_seed) % 1000;
        qubit_history[i].control_qubit_1 = xorshift32(&local_seed) % 1000;
        qubit_history[i].control_qubit_2 = -1; 
    }

    // Explicitly plant at least one needle so we guarantee a minimum of M=1
    int secret_index = xorshift32(&seed) % HISTORY_SIZE;
    qubit_history[secret_index].gate_type = GATE_TOFFOLI;
    qubit_history[secret_index].target_qubit = 42;         
    qubit_history[secret_index].control_qubit_1 = 100;
    qubit_history[secret_index].control_qubit_2 = 101;
    
    printf("Database Size : %d Gate Records\n", HISTORY_SIZE);
    printf("Search Target : TOFFOLI gate on Qubit 42\n");

    printf("\n--- EXECUTING DYNAMIC GROVER'S SUBROUTINE ---\n");
    double start_time = omp_get_wtime();

    int found_index = dynamic_grover_search(GATE_TOFFOLI, 42);

    double end_time = omp_get_wtime();

    printf("========== SUBROUTINE COMPLETE ==========\n");
    printf("Measured Index : [%d]\n", found_index);
    
    // Verification Check
    int is_match = (qubit_history[found_index].gate_type == GATE_TOFFOLI && 
                    qubit_history[found_index].target_qubit == 42);
                    
    printf("Match Success  : %s\n", is_match ? "TRUE" : "FALSE");
    
    if (is_match) {
        printf("Record Data    : Gate Type %d | Target Qubit %d\n", 
               qubit_history[found_index].gate_type, 
               qubit_history[found_index].target_qubit);
    }
    
    printf("Execution Time : %.4f seconds\n", end_time - start_time);
    printf("=========================================\n");

    free(qubit_history);
    free(amplitudes);
    return 0;
}
