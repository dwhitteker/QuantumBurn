#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>

#define NUM_QUBITS 20000   // Testing 100x your original target
#define MAX_LINKS 16       // Denser connectivity
#define TYPE_SYNC 0
#define TYPE_ANTI 1

typedef struct {
    float a_re, b_re;      // Using float to fit more in L2/L3 cache
    int links[MAX_LINKS];
    int link_types[MAX_LINKS];
    int num_links;
    int visited; 
} LinkedQubit;

// Align to 64-byte boundary for Zen 5 Cache Line efficiency
__attribute__((aligned(64))) LinkedQubit qubits[NUM_QUBITS];

void clear_visited() {
    for(int i = 0; i < NUM_QUBITS; i++) qubits[i].visited = 0;
}

// Optimized recursive broadcast
void broadcast_sync(int q) {
    qubits[q].visited = 1;
    for(int i = 0; i < qubits[q].num_links; i++) {
        int p = qubits[q].links[i];
        if (!qubits[p].visited) {
            if (qubits[q].link_types[i] == TYPE_SYNC) {
                qubits[p].a_re = qubits[q].a_re; 
                qubits[p].b_re = qubits[q].b_re;
            } else {
                qubits[p].a_re = qubits[q].b_re; 
                qubits[p].b_re = qubits[q].a_re;
            }
            broadcast_sync(p);
        }
    }
}

int main() {
    srand(42); // Deterministic seed for testing
    double start_time, end_time;

    printf("--- INITIALIZING %d QUBITS ---\n", NUM_QUBITS);
    for(int i = 0; i < NUM_QUBITS; i++) {
        qubits[i].a_re = 1.0f; qubits[i].b_re = 0.0f;
        qubits[i].num_links = 0;
        qubits[i].visited = 0;
        
        // Randomly link to 2-4 other qubits to create a complex mesh
        int links_to_create = 2 + (rand() % 3);
        for(int j=0; j < links_to_create; j++) {
            int target = rand() % NUM_QUBITS;
            if (target != i && qubits[i].num_links < MAX_LINKS && qubits[target].num_links < MAX_LINKS) {
                qubits[i].links[qubits[i].num_links] = target;
                qubits[i].link_types[qubits[i].num_links] = rand() % 2;
                qubits[i].num_links++;
                
                qubits[target].links[qubits[target].num_links] = i;
                qubits[target].link_types[qubits[target].num_links] = qubits[i].link_types[qubits[i].num_links];
                qubits[target].num_links++;
            }
        }
    }

    printf("--- STARTING GLOBAL CASCADE TEST ---\n");
    start_time = omp_get_wtime();

    // The "Hadamard" Trigger
    qubits[0].a_re = 0.707f; qubits[0].b_re = 0.707f;
    clear_visited();
    broadcast_sync(0);

    end_time = omp_get_wtime();
    
    // Check coverage
    int visited_count = 0;
    for(int i=0; i<NUM_QUBITS; i++) if(qubits[i].visited) visited_count++;

    printf("[RESULTS]\n");
    printf("Execution Time: %f seconds\n", end_time - start_time);
    printf("Qubits Entangled: %d / %d\n", visited_count, NUM_QUBITS);
    printf("Memory Footprint: ~%.2f MB\n", (float)(sizeof(qubits)) / (1024*1024));

    return 0;
}
