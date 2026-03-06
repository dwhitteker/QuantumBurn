#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>

#define NUM_QUBITS 1000000 
#define CYCLES 10000000000ULL  // 10 Billion Cycles
#define NOISE_FLOOR 0.0001 

// 64-Byte Cache-Aligned Qubit
typedef struct {
    float a_re, b_re;           // 8 bytes
    atomic_int checked_out;     // 4 bytes 
    int ghz_group_id;           // 4 bytes (The lowest ID of the triplet acts as the group signature)
    int state;                  // 4 bytes
    int noise_hits;             // 4 bytes
    int _padding[10];           // 40 bytes padding -> 64 bytes total
} Qubit;

Qubit* qubits;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

void apply_unitary_gate(int q, float theta) {
    float current_a = qubits[q].a_re;
    float current_b = qubits[q].b_re;
    float cos_t = cosf(theta);
    float sin_t = sinf(theta);
    
    qubits[q].a_re = current_a * cos_t - current_b * sin_t;
    qubits[q].b_re = current_a * sin_t + current_b * cos_t;
}

// Helper to sort 3 integers to prevent Dining Philosophers deadlock
void sort_three(int* a, int* b, int* c) {
    int tmp;
    if (*a > *b) { tmp = *a; *a = *b; *b = tmp; }
    if (*a > *c) { tmp = *a; *a = *c; *c = tmp; }
    if (*b > *c) { tmp = *b; *b = *c; *c = tmp; }
}

void entangle_ghz_triplet(int q1, int q2, int q3, uint32_t* seed) {
    // Cannot entangle a qubit with itself
    if (q1 == q2 || q1 == q3 || q2 == q3) return;
    
    int t[3] = {q1, q2, q3};
    sort_three(&t[0], &t[1], &t[2]);

    int expected = 0;
    
    // Attempt Lock 1 (Lowest ID)
    if (atomic_compare_exchange_strong(&qubits[t[0]].checked_out, &expected, 1)) {
        expected = 0;
        
        // Attempt Lock 2 (Middle ID)
        if (atomic_compare_exchange_strong(&qubits[t[1]].checked_out, &expected, 1)) {
            expected = 0;
            
            // Attempt Lock 3 (Highest ID)
            if (atomic_compare_exchange_strong(&qubits[t[2]].checked_out, &expected, 1)) {
                
                // --- SUCCESS: ALL 3 QUBITS SECURED ---
                // The lowest ID becomes the signature for this GHZ group
                int group_sig = t[0]; 
                
                qubits[t[0]].ghz_group_id = group_sig;
                qubits[t[1]].ghz_group_id = group_sig;
                qubits[t[2]].ghz_group_id = group_sig;

                // Apply GHZ State
                int outcome = xorshift32(seed) & 1;
                qubits[t[0]].state = outcome;
                qubits[t[1]].state = outcome;
                qubits[t[2]].state = outcome;

                // Apply diverging Unitary Gates to burn FPU cycles
                float angle = ((float)(xorshift32(seed) % 314159) / 100000.0f); 
                apply_unitary_gate(t[0], angle);
                apply_unitary_gate(t[1], angle * 1.5f); 
                apply_unitary_gate(t[2], -angle); 

                const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 
                if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[t[0]].noise_hits++;
                if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[t[1]].noise_hits++;
                if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[t[2]].noise_hits++;

                // Unlock 3
                atomic_store(&qubits[t[2]].checked_out, 0);
            }
            // Unlock 2 (happens whether Lock 3 succeeded or failed)
            atomic_store(&qubits[t[1]].checked_out, 0);
        }
        // Unlock 1 (happens whether Lock 2 succeeded or failed)
        atomic_store(&qubits[t[0]].checked_out, 0);
    }
}

int main() {
    omp_set_dynamic(0); 
    
    qubits = (Qubit*)aligned_alloc(64, sizeof(Qubit) * NUM_QUBITS);
    
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for(int i = 0; i < NUM_QUBITS; i++) {
        qubits[i].a_re = 1.0f;
        qubits[i].b_re = 0.0f;
        qubits[i].state = 0;
        qubits[i].noise_hits = 0;
        qubits[i].ghz_group_id = -1; 
        atomic_init(&qubits[i].checked_out, 0);
    }

    printf("--- GHZ TRIPLET BURN: 10 BILLION CYCLES | 64 THREADS ---\n");
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        #pragma omp for schedule(static, 100000)
        for(unsigned long long c = 0; c < CYCLES; c++) {
            int tA = xorshift32(&seed) % NUM_QUBITS;
            int tB = xorshift32(&seed) % NUM_QUBITS;
            int tC = xorshift32(&seed) % NUM_QUBITS;
            
            entangle_ghz_triplet(tA, tB, tC, &seed);
        }
    }

    double end_time = omp_get_wtime();
    
    long long total_noise_hits = 0;
    int active_triplets = 0;
    
    // Count active groups by looking for qubits whose group_id matches their own ID
    for(int i=0; i<NUM_QUBITS; i++) {
        total_noise_hits += qubits[i].noise_hits;
        if (qubits[i].ghz_group_id == i) active_triplets++;
    }

    printf("\n[RESULTS]\n");
    printf("Burn Time      : %.4f s\n", end_time - start_time);
    printf("Active GHZ     : %d distinct triplets survived\n", active_triplets);
    printf("Total Noise    : %lld gate errors injected\n", total_noise_hits);
    printf("Event Latency  : %.2f nanoseconds/attempt\n", ((end_time - start_time) / CYCLES) * 1e9);

    free(qubits);
    return 0;
}
