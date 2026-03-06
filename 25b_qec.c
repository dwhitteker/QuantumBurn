#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>

#define NUM_LOGICAL_QUBITS 1000000 
#define CYCLES 25000000000ULL  // 25 Billion Cycles
#define NOISE_FLOOR 0.01f      // 1.0% chance of a gate error per physical qubit

// 64-Byte Cache-Aligned Logical Qubit (1 Data + 2 Ancilla)
typedef struct {
    float a_re, b_re;           // 8 bytes (Simulated continuous state)
    atomic_int checked_out;     // 4 bytes (Thread Lock)
    
    int original_state;         // 4 bytes (Ground truth to verify our QEC math)
    int q_data;                 // 4 bytes (Physical Data Qubit)
    int q_ancilla_1;            // 4 bytes (Physical Helper 1)
    int q_ancilla_2;            // 4 bytes (Physical Helper 2)
    
    int errors_corrected;       // 4 bytes (Successful QEC interventions)
    int logical_failures;       // 4 bytes (When noise defeats the QEC)
    int _padding[7];            // 28 bytes padding -> exactly 64 bytes total
} LogicalQubit;

LogicalQubit* l_qubits;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

void apply_unitary_gate(int q, float theta) {
    float current_a = l_qubits[q].a_re;
    float current_b = l_qubits[q].b_re;
    float cos_t = cosf(theta);
    float sin_t = sinf(theta);
    
    l_qubits[q].a_re = current_a * cos_t - current_b * sin_t;
    l_qubits[q].b_re = current_a * sin_t + current_b * cos_t;
}

void process_qec_block(int target, uint32_t* seed) {
    int expected = 0;
    
    // Acquire Lock on the Logical Block
    if (atomic_compare_exchange_strong(&l_qubits[target].checked_out, &expected, 1)) {
        
        // --- 1. PREPARATION ---
        // Generate a new ground truth state
        int new_state = xorshift32(seed) & 1;
        l_qubits[target].original_state = new_state;
        
        // --- 2. ENCODING (Entanglement) ---
        // Sync the Ancilla qubits to the Data qubit
        l_qubits[target].q_data = new_state;
        l_qubits[target].q_ancilla_1 = new_state;
        l_qubits[target].q_ancilla_2 = new_state;

        // FPU Tax: Spin the continuous state to keep cores hot
        float angle = ((float)(xorshift32(seed) % 314159) / 100000.0f); 
        apply_unitary_gate(target, angle);

        // --- 3. NOISE INJECTION ---
        // Each physical qubit has an independent chance to flip
        const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 
        
        if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) {
            l_qubits[target].q_data ^= 1; // Bitwise XOR flip
        }
        if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) {
            l_qubits[target].q_ancilla_1 ^= 1; 
        }
        if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) {
            l_qubits[target].q_ancilla_2 ^= 1; 
        }

        // --- 4. SYNDROME MEASUREMENT & MAJORITY VOTE ---
        // We sum them. If sum is 0 or 3, they all match (or all flipped, which we can't detect).
        // If sum is 1, the majority is 0. If sum is 2, the majority is 1.
        int sum = l_qubits[target].q_data + l_qubits[target].q_ancilla_1 + l_qubits[target].q_ancilla_2;
        int corrected_state = (sum >= 2) ? 1 : 0;
        
        // Apply the correction to the main data qubit
        l_qubits[target].q_data = corrected_state;

        // --- 5. VERIFICATION ---
        // Did the QEC save us, or did the noise overwhelm the algorithm?
        if (l_qubits[target].q_data == l_qubits[target].original_state) {
            // If the sum was 1 or 2, an error happened and we successfully fixed it
            if (sum == 1 || sum == 2) {
                l_qubits[target].errors_corrected++;
            }
        } else {
            // Logical Failure! 
            // Two physical qubits flipped simultaneously, causing our majority vote 
            // to confidently "correct" the data into the WRONG state.
            l_qubits[target].logical_failures++;
        }

        // Release Lock
        atomic_store(&l_qubits[target].checked_out, 0);
    }
}

int main() {
    omp_set_dynamic(0); 
    
    l_qubits = (LogicalQubit*)aligned_alloc(64, sizeof(LogicalQubit) * NUM_LOGICAL_QUBITS);
    
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for(int i = 0; i < NUM_LOGICAL_QUBITS; i++) {
        l_qubits[i].a_re = 1.0f;
        l_qubits[i].b_re = 0.0f;
        l_qubits[i].original_state = 0;
        l_qubits[i].q_data = 0;
        l_qubits[i].q_ancilla_1 = 0;
        l_qubits[i].q_ancilla_2 = 0;
        l_qubits[i].errors_corrected = 0;
        l_qubits[i].logical_failures = 0;
        atomic_init(&l_qubits[i].checked_out, 0);
    }

    printf("--- QUANTUM ERROR CORRECTION BURN: 25 BILLION CYCLES | 64 THREADS ---\n");
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        #pragma omp for schedule(static, 100000)
        for(unsigned long long c = 0; c < CYCLES; c++) {
            int target = xorshift32(&seed) % NUM_LOGICAL_QUBITS;
            process_qec_block(target, &seed);
        }
    }

    double end_time = omp_get_wtime();
    
    long long total_saved = 0;
    long long total_destroyed = 0;

    for(int i=0; i<NUM_LOGICAL_QUBITS; i++) {
        total_saved += l_qubits[i].errors_corrected;
        total_destroyed += l_qubits[i].logical_failures;
    }

    printf("\n[RESULTS]\n");
    printf("Burn Time          : %.4f s\n", end_time - start_time);
    printf("Catastrophic Fails : %lld times (Double-flips broke the algorithm)\n", total_destroyed);
    printf("Successful QEC     : %lld states successfully repaired\n", total_saved);
    printf("Event Latency      : %.2f nanoseconds/operation\n", ((end_time - start_time) / CYCLES) * 1e9);

    free(l_qubits);
    return 0;
}
