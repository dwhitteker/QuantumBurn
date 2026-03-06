#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>

#define NUM_LOGICAL_QUBITS 1000000 
#define CYCLES 10000000000ULL  // 10 Billion Cycles
#define NOISE_FLOOR 0.05f      // 5.0% Noise Floor! It's a warzone in there.

// 64-Byte Cache-Aligned 2D Surface Qubit (5x5 Grid)
typedef struct {
    float a_re, b_re;           // 8 bytes
    atomic_int checked_out;     // 4 bytes 
    int original_state;         // 4 bytes 
    int errors_corrected;       // 4 bytes 
    int logical_failures;       // 4 bytes 
    int8_t grid[25];            // 25 bytes (Our 5x5 Physical Qubit Lattice)
    int8_t _padding[15];        // 15 bytes padding -> exactly 64 bytes total
} SurfaceQubit;

SurfaceQubit* s_qubits;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

void apply_unitary_gate(int q, float theta) {
    float current_a = s_qubits[q].a_re;
    float current_b = s_qubits[q].b_re;
    float cos_t = cosf(theta);
    float sin_t = sinf(theta);
    
    s_qubits[q].a_re = current_a * cos_t - current_b * sin_t;
    s_qubits[q].b_re = current_a * sin_t + current_b * cos_t;
}

void process_surface_patch(int target, uint32_t* seed) {
    int expected = 0;
    
    if (atomic_compare_exchange_strong(&s_qubits[target].checked_out, &expected, 1)) {
        
        // --- 1. PREPARATION & ENCODING ---
        int new_state = xorshift32(seed) & 1;
        s_qubits[target].original_state = new_state;
        
        // Flood the 5x5 grid with the entangled state
        for (int i = 0; i < 25; i++) {
            s_qubits[target].grid[i] = new_state;
        }

        float angle = ((float)(xorshift32(seed) % 314159) / 100000.0f); 
        apply_unitary_gate(target, angle);

        // --- 2. NOISE INJECTION ---
        const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 
        int errors_present = 0;
        
        // Carpet bomb the 2D lattice with 5% noise
        for (int i = 0; i < 25; i++) {
            if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) {
                s_qubits[target].grid[i] ^= 1; 
                errors_present++;
            }
        }

        // --- 3. TOPOLOGICAL DECODING (DFS Percolation Check) ---
        // If there are no errors, we skip the heavy math
        if (errors_present > 0) {
            
            int spanned = 0;
            int visited[25] = {0};
            int stack[25];
            int top = -1;

            // Step A: Find any corrupted nodes on the LEFT boundary (Column 0)
            for (int r = 0; r < 5; r++) {
                int idx = r * 5; 
                if (s_qubits[target].grid[idx] != new_state) {
                    stack[++top] = idx;
                    visited[idx] = 1;
                }
            }

            // Step B: Depth-First Search to see if the noise forms a bridge
            while (top >= 0) {
                int curr = stack[top--];
                int r = curr / 5;
                int c = curr % 5;

                // Did the noise reach the RIGHT boundary? (Column 4)
                if (c == 4) { 
                    spanned = 1; 
                    break; 
                }

                // Check Adjacent Neighbors (Up, Down, Left, Right)
                int neighbors[4] = {curr - 5, curr + 5, curr - 1, curr + 1};
                
                for (int i = 0; i < 4; i++) {
                    int n = neighbors[i];
                    
                    // Bounds checking
                    if (n >= 0 && n < 25) {
                        // Prevent teleporting across rows
                        if (i == 2 && c == 0) continue; // Trying to go left from column 0
                        if (i == 3 && c == 4) continue; // Trying to go right from column 4

                        if (!visited[n] && s_qubits[target].grid[n] != new_state) {
                            visited[n] = 1;
                            stack[++top] = n;
                        }
                    }
                }
            }

            // --- 4. VERIFICATION ---
            if (spanned) {
                // The noise bridged the gap! Logical state destroyed.
                s_qubits[target].logical_failures++;
            } else {
                // The surface code successfully absorbed the scattered noise.
                s_qubits[target].errors_corrected++;
            }
        }

        atomic_store(&s_qubits[target].checked_out, 0);
    }
}

int main() {
    omp_set_dynamic(0); 
    
    s_qubits = (SurfaceQubit*)aligned_alloc(64, sizeof(SurfaceQubit) * NUM_LOGICAL_QUBITS);
    
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for(int i = 0; i < NUM_LOGICAL_QUBITS; i++) {
        s_qubits[i].a_re = 1.0f;
        s_qubits[i].b_re = 0.0f;
        s_qubits[i].original_state = 0;
        s_qubits[i].errors_corrected = 0;
        s_qubits[i].logical_failures = 0;
        atomic_init(&s_qubits[i].checked_out, 0);
    }

    printf("--- 2D SURFACE CODE (5x5 GRID): 10 BILLION CYCLES | 64 THREADS ---\n");
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        #pragma omp for schedule(static, 100000)
        for(unsigned long long c = 0; c < CYCLES; c++) {
            int target = xorshift32(&seed) % NUM_LOGICAL_QUBITS;
            process_surface_patch(target, &seed);
        }
    }

    double end_time = omp_get_wtime();
    
    long long total_saved = 0;
    long long total_destroyed = 0;

    for(int i = 0; i < NUM_LOGICAL_QUBITS; i++) {
        total_saved += s_qubits[i].errors_corrected;
        total_destroyed += s_qubits[i].logical_failures;
    }

    printf("\n[RESULTS]\n");
    printf("Burn Time          : %.4f s\n", end_time - start_time);
    printf("Catastrophic Fails : %lld times (Noise bridged the 5x5 grid)\n", total_destroyed);
    printf("Successful QEC     : %lld states successfully repaired\n", total_saved);
    printf("Event Latency      : %.2f nanoseconds/operation\n", ((end_time - start_time) / CYCLES) * 1e9);

    free(s_qubits);
    return 0;
}
