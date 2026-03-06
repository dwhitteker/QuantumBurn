# QuantumBurn: Bare-Metal Quantum Simulator

**QuantumBurn** is a high-performance, classical CPU simulator for quantum mechanical concepts and algorithms. Written in C and optimized for high-core-count processors, this suite acts as both a narrative educational tool for quantum mechanics and an aggressive, bare-metal CPU stress test.

Instead of hiding the math behind high-level API calls, this suite forces the CPU's Arithmetic Logic Units (ALUs) and Floating-Point Units (FPUs) to physically calculate the wave interference, phase shifts, and state collapses in real-time across millions of simulated qubits.

## Hardware Optimizations
This suite is designed to push multi-core processors (tested on 64-thread, water-cooled architectures) to their absolute thermal and computational limits.
* **Cache-Line Alignment:** Qubit structs are aggressively padded to exactly 64 bytes to perfectly match CPU L1 cache lines, eliminating False Sharing and cache invalidation thrashing.
* **Lock-Free Concurrency:** Implements hardware-level `stdatomic.h` Compare-And-Swap (CAS) operations to avoid OS-level mutex overhead.
* **Deadlock Evasion:** Complex multi-qubit entanglements (like 3-way GHZ states and Teleportation) utilize strictly ordered memory-address sorting to prevent the "Dining Philosophers" deadlock under extreme thread contention.
* **Core Affinity:** Utilizes OpenMP `proc_bind(spread)` to bypass OS thread scheduling and pin workloads directly to physical cores for maximum throughput.

## The Architecture: L1-Aligned Zero-Latency Routing

To achieve sub-40-second execution times on massive datasets, the CPU's Arithmetic Logic Units (ALUs) must be fed data without waiting on main memory (RAM) lookups. 

This architecture abandons the traditional global state-vector tensor product. Instead, every qubit is treated as an independent node in a 1D C array. 

By aggressively padding the `LogicalQubit` struct to exactly 64 bytes, it perfectly maps to a single x86 L1 cache line. This physically isolates each qubit in the CPU cache, completely eliminating false-sharing cache invalidations when 64 threads hit the array simultaneously.

But rather than leaving the padding as dead space, the remaining 48 bytes are utilized as a **high-speed, localized instruction buffer**. 



Using a 64-bit packed `union`, each qubit carries its own routing data, entanglement targets, and physics flags. When a thread pulls a qubit into the L1 cache, the memory controller pulls its next 6 scheduled operations and target pointers in the exact same clock cycle. This allows the OpenMP threads to execute complex entanglements (like GHZ and Bell States) without ever halting for a RAM lookup.

```c
#include <stdint.h>
#include <stdatomic.h>

// ---------------------------------------------------------
// 64-Bit Packed Metadata Word (8 Bytes Total)
// ---------------------------------------------------------
typedef union {
    uint64_t raw_data; // AVX-512 / ALU ready 
    struct {
        uint64_t updated_flag    : 1;  // Processed this tick?
        uint64_t gate_type       : 4;  // Gate instruction (H, CNOT, etc.)
        uint64_t router_array    : 4;  // Real/Imag data source
        uint64_t router_stack    : 4;  // Cluster Stack ID
        uint64_t block_id        : 6;  // Target within the 64-byte stack
        uint64_t is_syndrome     : 1;  // Surface Code: Data vs Measure
        uint64_t virtual_z_flip  : 1;  // Track Phase/Sign flips
        uint64_t noise_injected  : 1;  // Decoherence tracking
        uint64_t is_entangled    : 1;  // Active entanglement flag
        uint64_t reserved_flag   : 1;  
        uint64_t target_qubit_id : 40; // Addresses up to 1.09 Trillion qubits
    } __attribute__((packed));
} QubitMetadata;

// ---------------------------------------------------------
// The Final L1-Aligned Qubit Node
// ---------------------------------------------------------
typedef struct {
    // 1. Quantum State (8 bytes)
    float amplitude_real;
    float amplitude_imag;
    
    // 2. Concurrency & ID (8 bytes)
    _Atomic int lock;    
    int id;              
    
    // 3. The Hardware Optimization (48 bytes)
    // 48 bytes / 8 bytes = Exactly 6 slots.
    // Every qubit carries its own localized history and routing targets
    // directly inside the L1 cache line.
    QubitMetadata ops[6]; 

} __attribute__((aligned(64))) LogicalQubit;

## The Simulation Suite

### 1. Entanglement & Decoherence (`25b_2d_grid.c`)
Simulates the "Monogamy of Entanglement." 64 threads randomly pair qubits across a 1-million qubit array, severing old bonds to form new ones while performing heavy unitary matrix rotations to simulate continuous phase states.

### 2. GHZ Triplet State (`10b_ghz_triplet.c`)
Tests extreme concurrency by forcing threads to negotiate 3-way simultaneous locks to entangle triplets of qubits without deadlocking the system. 

### 3. Quantum Teleportation (`10b_teleport.c`)
Enforces the "No-Cloning Theorem." Threads lock a Source, Channel, and Target qubit, mathematically transferring the complex amplitude from the Source to the Target while immediately destroying the Source state.

### 4. Quantum Error Correction (1D Repetition & 2D Surface Codes)
Simulates quantum fault tolerance against physical gate noise. This repository includes three distinct stages of QEC scaling:
* **Distance-3 Repetition (`25b_qec.c`):** A 3-qubit bit-flip code that uses majority-vote syndrome measurements to repair single errors.
* **Distance-7 Repetition (`25b_qec_7qubit.c`):** Expands the logical block to 7 physical qubits, demonstrating how adding ancilla qubits exponentially suppresses the binomial probability of catastrophic double/triple flips.
* **2D Surface Code (`2d_surface_code.c`):** Simulates Google/IBM's topological architecture. Uses a 5x5 lattice within a 64-byte struct, actively running a Depth-First Search (DFS) percolation algorithm to ensure quantum noise hasn't bridged the grid boundaries.

### 5. Shor's Algorithm (`shors_sim.c`)
A massive mathematical race. 64 threads use 128-bit modular exponentiation to brute-force the quantum wave period of a 40-bit RSA semiprime target, cracking the encryption factor in seconds.

### 6. Grover's Algorithm & QRAM (`grover.c` & `grover_db.c`)
Demonstrates Quantum Amplitude Amplification. Uses a simulated Quantum Counting subroutine to dynamically tune the algorithm, finding a single hidden record inside a 1-million-item unstructured database using geometric wave reflections.

### 7. BB84 Quantum Key Distribution (`bb84_sim.c`)
Simulates quantum cryptography. Alice and Bob attempt to generate a secure key using randomized photon polarization bases, while an eavesdropper (Eve) inadvertently triggers wave collapses and mathematically reveals her presence through a spiked Quantum Error Rate.

## Build and Run Instructions

**Prerequisites:** GCC compiler with OpenMP support.

For maximum hardware utilization, compile with the following aggressive flags:
```bash
gcc -O3 -march=native -ffast-math -fopenmp <filename.c> -o quantum_burn -lm
