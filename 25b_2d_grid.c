#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>

#define NUM_QUBITS 1000000 
#define CYCLES 25000000000ULL  // 25 Billion Cycles
#define NOISE_FLOOR 0.0001 

// 64-Byte Cache-Aligned Qubit
typedef struct {
    float a_re, b_re;           // 8 bytes
    atomic_int checked_out;     // 4 bytes 
    int partner_id;             // 4 bytes 
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

void sever_old_partner(int old_partner_id) {
    if (old_partner_id == -1) return;

    int expected_free = 0;
    if (atomic_compare_exchange_strong(&qubits[old_partner_id].checked_out, &expected_free, 1)) {
        qubits[old_partner_id].partner_id = -1; 
        qubits[old_partner_id].state = 0;       
        atomic_store(&qubits[old_partner_id].checked_out, 0);
    }
}

void entangle_pair(int q1, int q2, uint32_t* seed) {
    if (q1 == q2) return;
    
    int first = (q1 < q2) ? q1 : q2;
    int second = (q1 > q2) ? q1 : q2;

    int expected = 0;
    
    if (atomic_compare_exchange_strong(&qubits[first].checked_out, &expected, 1)) {
        expected = 0;
        
        if (atomic_compare_exchange_strong(&qubits[second].checked_out, &expected, 1)) {
            
            sever_old_partner(qubits[first].partner_id);
            sever_old_partner(qubits[second].partner_id);

            qubits[first].partner_id = second;
            qubits[second].partner_id = first;

            float random_angle = ((float)(xorshift32(seed) % 314159) / 100000.0f); 
            apply_unitary_gate(first, random_angle);
            apply_unitary_gate(second, -random_angle); 

            int outcome = xorshift32(seed) & 1;
            qubits[first].state = outcome;
            qubits[second].state = outcome;

            const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 
            if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[first].noise_hits++;
            if ((float)xorshift32(seed) * UINT32_MAX_INV < NOISE_FLOOR) qubits[second].noise_hits++;

            atomic_store(&qubits[second].checked_out, 0);
        }
        atomic_store(&qubits[first].checked_out, 0);
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
        qubits[i].partner_id = -1; 
        atomic_init(&qubits[i].checked_out, 0);
    }

    printf("--- MONOGAMY ENTANGLEMENT BURN: 25 BILLION CYCLES | 64 THREADS ---\n");
    printf("--- YOU HAVE ABOUT 80 SECONDS... OPEN BTOP NOW! ---\n");
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        #pragma omp for schedule(static, 100000)
        for(unsigned long long c = 0; c < CYCLES; c++) {
            int target_A = xorshift32(&seed) % NUM_QUBITS;
            int target_B = xorshift32(&seed) % NUM_QUBITS;
            
            entangle_pair(target_A, target_B, &seed);
        }
    }

    double end_time = omp_get_wtime();
    
    long long total_noise_hits = 0;
    int active_pairs = 0;
    for(int i=0; i<NUM_QUBITS; i++) {
        total_noise_hits += qubits[i].noise_hits;
        if (qubits[i].partner_id != -1) active_pairs++;
    }

    printf("\n[RESULTS]\n");
    printf("Burn Time      : %.4f s\n", end_time - start_time);
    printf("Active Pairs   : %d / %d Qubits paired at simulation end\n", active_pairs, NUM_QUBITS);
    printf("Total Noise    : %lld gate errors injected\n", total_noise_hits);
    printf("Event Latency  : %.2f nanoseconds/entanglement\n", ((end_time - start_time) / CYCLES) * 1e9);

    printf("\n--- DIAGNOSTIC: 5 SURVIVING ENTANGLED PAIRS ---\n");
    int printed = 0;
    for(int i = 0; i < NUM_QUBITS && printed < 5; i++) {
        // Only print if paired, and only print from the lower ID to avoid duplicates
        if (qubits[i].partner_id != -1 && i < qubits[i].partner_id) {
            int p = qubits[i].partner_id;
            printf("Qubit[%06d] & Qubit[%06d] | Shared State: %d | Q1(a: % .4f, b: % .4f) Q2(a: % .4f, b: % .4f)\n", 
                   i, p, qubits[i].state, 
                   qubits[i].a_re, qubits[i].b_re, 
                   qubits[p].a_re, qubits[p].b_re);
            printed++;
        }
    }

    free(qubits);
    return 0;
}
