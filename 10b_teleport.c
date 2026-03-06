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
    int teleport_count;         // 4 bytes 
    int state;                  // 4 bytes
    int noise_hits;             // 4 bytes
    int _padding[10];           // 40 bytes padding -> 64 bytes total
} Qubit;

Qubit* qubits;
atomic_llong total_teleportations = 0;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

// Heavy FPU rotation to simulate phase shifts during teleportation
void apply_phase_shift(int q, float theta) {
    float current_a = qubits[q].a_re;
    float current_b = qubits[q].b_re;
    float cos_t = cosf(theta);
    float sin_t = sinf(theta);
    
    qubits[q].a_re = current_a * cos_t - current_b * sin_t;
    qubits[q].b_re = current_a * sin_t + current_b * cos_t;
}

// Sort to prevent Deadlocks
void sort_three(int* a, int* b, int* c) {
    int tmp;
    if (*a > *b) { tmp = *a; *a = *b; *b = tmp; }
    if (*a > *c) { tmp = *a; *a = *c; *c = tmp; }
    if (*b > *c) { tmp = *b; *b = *c; *c = tmp; }
}

void quantum_teleport(int src, int channel, int target, uint32_t* seed) {
    if (src == channel || src == target || channel == target) return;
    
    int t[3] = {src, channel, target};
    sort_three(&t[0], &t[1], &t[2]);

    int expected = 0;
    
    // Acquire Locks in strict memory order
    if (atomic_compare_exchange_strong(&qubits[t[0]].checked_out, &expected, 1)) {
        expected = 0;
        if (atomic_compare_exchange_strong(&qubits[t[1]].checked_out, &expected, 1)) {
            expected = 0;
            if (atomic_compare_exchange_strong(&qubits[t[2]].checked_out, &expected, 1)) {
                
                // --- TELEPORTATION PROTOCOL ---
                
                // 1. Transfer the state from Source to Target
                qubits[target].a_re = qubits[src].a_re;
                qubits[target].b_re = qubits[src].b_re;
                qubits[target].state = qubits[src].state;

                // 2. No-Cloning Theorem Enforcement: Destroy the Source state
                qubits[src].a_re = 1.0f; 
                qubits[src].b_re = 0.0f;
                qubits[src].state = 0;

                // 3. Apply heavy phase shift to Target to simulate channel distortion
                float angle = ((float)(xorshift32(seed) % 314159) / 100000.0f); 
                apply_phase_shift(target, angle);

                // 4. Record the successful event
                qubits[target].teleport_count++;
                atomic_fetch_add(&total_teleportations, 1);

                // Noise Generation
                const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 
                if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[channel].noise_hits++;

                // Release Locks
                atomic_store(&qubits[t[2]].checked_out, 0);
            }
            atomic_store(&qubits[t[1]].checked_out, 0);
        }
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
        qubits[i].teleport_count = 0;
        qubits[i].noise_hits = 0;
        atomic_init(&qubits[i].checked_out, 0);
    }

    printf("--- QUANTUM TELEPORTATION BURN: 10 BILLION CYCLES | 64 THREADS ---\n");
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        #pragma omp for schedule(static, 100000)
        for(unsigned long long c = 0; c < CYCLES; c++) {
            // Pick Source, Channel, and Target
            int src = xorshift32(&seed) % NUM_QUBITS;
            int channel = xorshift32(&seed) % NUM_QUBITS;
            int target = xorshift32(&seed) % NUM_QUBITS;
            
            // Generate a random state to inject into the Source before teleporting
            int initial_state = xorshift32(&seed) & 1;
            float initial_angle = ((float)(xorshift32(&seed) % 314159) / 100000.0f);
            
            // We bypass locks for this quick injection to simulate external data input
            qubits[src].a_re = (initial_state == 0) ? 1.0f : 0.0f;
            qubits[src].b_re = (initial_state == 0) ? 0.0f : 1.0f;
            qubits[src].state = initial_state;
            apply_phase_shift(src, initial_angle);

            // Execute Teleport
            quantum_teleport(src, channel, target, &seed);
        }
    }

    double end_time = omp_get_wtime();
    
    long long total_noise_hits = 0;
    int max_teleports_received = 0;
    int hottest_qubit = 0;

    for(int i=0; i<NUM_QUBITS; i++) {
        total_noise_hits += qubits[i].noise_hits;
        if (qubits[i].teleport_count > max_teleports_received) {
            max_teleports_received = qubits[i].teleport_count;
            hottest_qubit = i;
        }
    }

    printf("\n[RESULTS]\n");
    printf("Burn Time          : %.4f s\n", end_time - start_time);
    printf("Successful Jumps   : %lld states teleported\n", atomic_load(&total_teleportations));
    printf("Channel Noise      : %lld gate errors injected\n", total_noise_hits);
    printf("Hottest Qubit      : Qubit[%06d] received %d teleports\n", hottest_qubit, max_teleports_received);
    printf("Lock Success Rate  : %.2f%%\n", ((float)atomic_load(&total_teleportations) / CYCLES) * 100.0f);

    free(qubits);
    return 0;
}
