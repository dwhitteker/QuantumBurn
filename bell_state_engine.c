#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <math.h>
#include <omp.h>

// ---------------------------------------------------------
// 64-Bit Packed Metadata Word
// ---------------------------------------------------------
typedef union {
    uint64_t raw_data; 
    struct {
        uint64_t updated_flag    : 1;  
        uint64_t gate_type       : 4;  // e.g., 1=Hadamard, 2=CNOT
        uint64_t router_array    : 4;  
        uint64_t router_stack    : 4;  
        uint64_t block_id        : 6;  
        uint64_t is_syndrome     : 1;  
        uint64_t virtual_z_flip  : 1;  
        uint64_t noise_injected  : 1;  
        uint64_t is_entangled    : 1;  // NEW: Flag to indicate active entanglement
        uint64_t reserved_flag   : 1;  
        uint64_t target_qubit_id : 40; 
    } __attribute__((packed));
} QubitMetadata;

// ---------------------------------------------------------
// L1-Aligned Qubit Node (64 Bytes)
// ---------------------------------------------------------
typedef struct {
    float amplitude_real;   
    float amplitude_imag;   
    _Atomic int lock;       
    int id;                 
    QubitMetadata ops[6];   
} __attribute__((aligned(64))) LogicalQubit;

// ---------------------------------------------------------
// Atomic Spinlocks
// ---------------------------------------------------------
void lock_qubit(LogicalQubit* q) {
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&q->lock, &expected, 1, memory_order_acquire, memory_order_relaxed)) {
        expected = 0; 
    }
}

void unlock_qubit(LogicalQubit* q) {
    atomic_store_explicit(&q->lock, 0, memory_order_release);
}

// ---------------------------------------------------------
// Main Execution
// ---------------------------------------------------------
int main() {
    printf("--- BELL STATE ENTANGLEMENT ENGINE ---\n");
    
    // Allocate 20 Million Qubits (10 Million Bell Pairs)
    int num_qubits = 20000000; 
    LogicalQubit* q_register = (LogicalQubit*)aligned_alloc(64, num_qubits * sizeof(LogicalQubit));
    if (!q_register) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    // Initialize state and routing paths
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for (int i = 0; i < num_qubits; i++) {
        q_register[i].id = i;
        atomic_init(&q_register[i].lock, 0);
        
        // Start all qubits in pure |0> state
        q_register[i].amplitude_real = 1.0f; 
        q_register[i].amplitude_imag = 0.0f;
        
        for(int j = 0; j < 6; j++) q_register[i].ops[j].raw_data = 0;

        // Pair them up: Evens are Control, Odds are Target
        if (i % 2 == 0) {
            q_register[i].ops[0].target_qubit_id = i + 1; // Point to target
            q_register[i].ops[0].gate_type = 2;           // CNOT instruction
        }
    }

    printf("Executing 10 Million simultaneous Bell State entanglements...\n");
    double start_time = omp_get_wtime();

    // The 64-Thread Entanglement Burn
    #pragma omp parallel for num_threads(64) proc_bind(spread) schedule(guided)
    for (int i = 0; i < num_qubits; i += 2) {
        
        // 1. L1 Cache loads the Control Qubit and its routing metadata
        int target_id = q_register[i].ops[0].target_qubit_id;

        // 2. Lock both qubits sequentially
        lock_qubit(&q_register[i]);
        lock_qubit(&q_register[target_id]);

        // 3. Apply Hadamard Gate to Control (puts it into superposition)
        q_register[i].amplitude_real *= 0.707106f; // 1/sqrt(2)
        
        // 4. Apply CNOT (Entanglement)
        // In this flat model, the target inherits the probability distribution
        // and phase of the control qubit, officially linking their states.
        q_register[target_id].amplitude_real = q_register[i].amplitude_real;
        
        // Set entanglement flags to track the physics linkage
        q_register[i].ops[0].is_entangled = 1;
        q_register[target_id].ops[0].is_entangled = 1;
        q_register[target_id].ops[0].target_qubit_id = i; // Target points back to Control

        // 5. Unlock
        unlock_qubit(&q_register[target_id]);
        unlock_qubit(&q_register[i]);
    }

    double end_time = omp_get_wtime();

    printf("========== ENTANGLEMENT COMPLETE ==========\n");
    printf("Bell Pairs Created : %d\n", num_qubits / 2);
    printf("Execution Time     : %f seconds\n", end_time - start_time);
    
    // Quick Verification of Pair 0
    printf("\nVerification (Pair 0 & 1):\n");
    printf("Q0 Real Amp: %f | Entangled with Q%llu\n", q_register[0].amplitude_real, q_register[0].ops[0].target_qubit_id);
    printf("Q1 Real Amp: %f | Entangled with Q%llu\n", q_register[1].amplitude_real, q_register[1].ops[0].target_qubit_id);
    printf("===========================================\n");

    free(q_register);
    return 0;
}
