#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>
#include <time.h>

// Search space: 2^24 items (16.7 Million)
#define NUM_STATES 16777216 

float* amplitudes;

int main() {
    omp_set_dynamic(0);
    
    // Allocate 64MB for our quantum state amplitudes
    amplitudes = (float*)aligned_alloc(64, sizeof(float) * NUM_STATES);
    
    // Pick a random needle in the 16.7 million haystack
    srand(time(NULL));
    int target_id = (rand() * rand()) % NUM_STATES;
    
    printf("--- GROVER'S ALGORITHM: AMPLITUDE AMPLIFICATION ---\n");
    printf("Database Size  : %d items\n", NUM_STATES);
    printf("Hidden Target  : Index [%d]\n", target_id);
    
    // Classical search would take N/2 checks on average (~8.3 Million checks)
    // Grover's takes exactly (pi / 4) * sqrt(N) iterations
    int grover_iterations = (int)((M_PI / 4.0) * sqrt(NUM_STATES));
    printf("Required Steps : %d Quantum Iterations\n\n", grover_iterations);

    double start_time = omp_get_wtime();

    // 1. INITIALIZATION: Create a perfect superposition
    // The sum of all probabilities (amplitude^2) must equal 1.0
    float initial_amp = 1.0f / sqrt((float)NUM_STATES);
    
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for (int i = 0; i < NUM_STATES; i++) {
        amplitudes[i] = initial_amp;
    }

    // 2. THE QUANTUM LOOP (Oracle + Diffusion)
    for (int iter = 0; iter < grover_iterations; iter++) {
        
        // --- THE ORACLE ---
        // Identify the target and flip its phase (wave goes upside down)
        // In real quantum computing, this is a black-box function
        amplitudes[target_id] = -amplitudes[target_id];

        // --- THE DIFFUSION OPERATOR (Wave Interference) ---
        // Step A: Calculate the global average amplitude of the entire system
        double global_sum = 0.0;
        
        #pragma omp parallel for reduction(+:global_sum) num_threads(64)
        for (int i = 0; i < NUM_STATES; i++) {
            global_sum += amplitudes[i];
        }
        
        float average_amp = (float)(global_sum / NUM_STATES);

        // Step B: Reflect every amplitude around the average
        // This causes the negative target to massively amplify, and all others to shrink
        #pragma omp parallel for num_threads(64)
        for (int i = 0; i < NUM_STATES; i++) {
            amplitudes[i] = (2.0f * average_amp) - amplitudes[i];
        }
        
        // Optional: Print the wave growing every 500 steps to watch the interference
        if (iter % 500 == 0 || iter == grover_iterations - 1) {
            float target_prob = amplitudes[target_id] * amplitudes[target_id];
            printf("Iteration %4d | Target Probability: %8.4f%% | Non-Target Amp: %.8f\n", 
                   iter, target_prob * 100.0f, amplitudes[(target_id + 1) % NUM_STATES]);
        }
    }

    double end_time = omp_get_wtime();

    // 3. MEASUREMENT
    // Find the state with the highest probability
    int measured_result = 0;
    float max_prob = 0.0f;
    
    for (int i = 0; i < NUM_STATES; i++) {
        float prob = amplitudes[i] * amplitudes[i];
        if (prob > max_prob) {
            max_prob = prob;
            measured_result = i;
        }
    }

    printf("\n========== MEASUREMENT COLLAPSE ==========\n");
    printf("Measured Index : [%d]\n", measured_result);
    printf("Match Success  : %s\n", (measured_result == target_id) ? "TRUE" : "FALSE");
    printf("Final Certainty: %.4f%%\n", max_prob * 100.0f);
    printf("Execution Time : %.4f seconds\n", end_time - start_time);
    printf("==========================================\n");

    free(amplitudes);
    return 0;
}
