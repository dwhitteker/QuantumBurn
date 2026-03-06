#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <omp.h>

// ---------------------------------------------------------
// 64-Bit Packed Metadata Word (8 Bytes)
// ---------------------------------------------------------
typedef union {
    uint64_t raw_data; 
    struct {
        uint64_t updated_flag    : 1;  
        uint64_t gate_type       : 4;  
        uint64_t router_array    : 4;  
        uint64_t router_stack    : 4;  
        uint64_t block_id        : 6;  
        uint64_t is_syndrome     : 1;  
        uint64_t virtual_z_flip  : 1;  
        uint64_t noise_injected  : 1;  
        uint64_t reserved_flags  : 2;  
        uint64_t target_qubit_id : 40; 
    } __attribute__((packed));
} QubitMetadata;

// ---------------------------------------------------------
// The L1-Aligned Qubit Node (64 Bytes Total)
// ---------------------------------------------------------
typedef struct {
    float amplitude_real;   // 4 bytes
    float amplitude_imag;   // 4 bytes
    _Atomic int lock;       // 4 bytes
    int id;                 // 4 bytes
    QubitMetadata ops[6];   // 48 bytes (6 * 8 bytes)
} __attribute__((aligned(64))) LogicalQubit;

// ---------------------------------------------------------
// Atomic Spinlock Functions
// ---------------------------------------------------------
void lock_qubit(LogicalQubit* q) {
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&q->lock, &expected, 1, memory_order_acquire, memory_order_relaxed)) {
        expected = 0; // Reset expected if CAS fails
    }
}

void unlock_qubit(LogicalQubit* q) {
    atomic_store_explicit(&q->lock, 0, memory_order_release);
}

// ---------------------------------------------------------
// Main Execution
// ---------------------------------------------------------
int main() {
    printf("--- L1 CACHE ALIGNMENT & ROUTING TEST ---\n");
    
    // 1. Verify Architecture Size
    size_t struct_size = sizeof(LogicalQubit);
    printf("Size of LogicalQubit struct : %zu bytes\n", struct_size);
    if (struct_size != 64) {
        printf("ERROR: Struct is not 64 bytes! Compiler padding mismatch.\n");
        return 1;
    }
    printf("Struct alignment verified. Zero cache-line spill guaranteed.\n\n");

    // 2. Allocate 30 Million Qubits (10 Million GHZ Triplets)
    // Using aligned_alloc to ensure the start of the array aligns with cache boundaries
    int num_qubits = 30000000; 
    printf("Allocating %.2f GB of linear Qubit memory...\n", (num_qubits * 64.0) / (1024*1024*1024));
    
    LogicalQubit* q_register = (LogicalQubit*)aligned_alloc(64, num_qubits * sizeof(LogicalQubit));
    if (!q_register) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    // 3. Initialize the Routing Data
    // We pre-load the 48-byte buffer with the entanglement targets
    #pragma omp parallel for num_threads(64) proc_bind(spread)
    for (int i = 0; i < num_qubits; i++) {
        q_register[i].id = i;
        atomic_init(&q_register[i].lock, 0);
        q_register[i].amplitude_real = 1.0f; // Start in |0>
        q_register[i].amplitude_imag = 0.0f;
        
        // Clear metadata
        for(int j=0; j<6; j++) q_register[i].ops[j].raw_data = 0;

        // If this is the start of a triplet (i % 3 == 0)
        // Store pointers to the next two qubits directly in the padding buffer
        if (i % 3 == 0 && i + 2 < num_qubits) {
            q_register[i].ops[0].target_qubit_id = i + 1; // Target for CNOT 1
            q_register[i].ops[0].gate_type = 2;           // Arbitrary ID for CNOT
            
            q_register[i].ops[1].target_qubit_id = i + 2; // Target for CNOT 2
            q_register[i].ops[1].gate_type = 2;
        }
    }

    printf("--- EXECUTING 10 MILLION GHZ ENTANGLEMENTS ---\n");
    double start_time = omp_get_wtime();

    // 4. The 64-Thread Stress Test
    // Threads jump through the array, read the targets FROM THE STRUCT ITSELF, and execute.
    #pragma omp parallel for num_threads(64) proc_bind(spread) schedule(guided)
    for (int i = 0; i < num_qubits; i += 3) {
        
        // Grab targets directly from the L1-cached struct buffer (no RAM lookup!)
        int target_1 = q_register[i].ops[0].target_qubit_id;
        int target_2 = q_register[i].ops[1].target_qubit_id;

        // Lock all three sequentially to prevent deadlocks
        lock_qubit(&q_register[i]);
        lock_qubit(&q_register[target_1]);
        lock_qubit(&q_register[target_2]);

        // Simulate FPU math for GHZ State (Hadamard then 2x CNOT)
        // 0.7071f is 1/sqrt(2)
        q_register[i].amplitude_real *= 0.7071f; 
        q_register[target_1].amplitude_real = q_register[i].amplitude_real;
        q_register[target_2].amplitude_real = q_register[i].amplitude_real;

        // Mark as updated using the bitfield
        q_register[i].ops[0].updated_flag = 1;

        // Unlock
        unlock_qubit(&q_register[target_2]);
        unlock_qubit(&q_register[target_1]);
        unlock_qubit(&q_register[i]);
    }

    double end_time = omp_get_wtime();

    printf("========== GHZ ROUTING COMPLETE ==========\n");
    printf("Total Qubits Processed : %d\n", num_qubits);
    printf("GHZ States Created     : %d\n", num_qubits / 3);
    printf("Execution Time         : %f seconds\n", end_time - start_time);
    printf("==========================================\n");

    free(q_register);
    return 0;
}
