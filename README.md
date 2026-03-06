# QuantumBurn: Bare-Metal Quantum Simulator

**QuantumBurn** is a high-performance, classical CPU simulator for quantum mechanical concepts and algorithms. Written in C and optimized for high-core-count processors, this suite acts as both a narrative educational tool for quantum mechanics and an aggressive, bare-metal CPU stress test.

Instead of hiding the math behind high-level API calls, this suite forces the CPU's Arithmetic Logic Units (ALUs) and Floating-Point Units (FPUs) to physically calculate the wave interference, phase shifts, and state collapses in real-time across millions of simulated qubits.

## Hardware Optimizations
This suite is designed to push multi-core processors (tested on 64-thread, water-cooled architectures) to their absolute thermal and computational limits.
* **Cache-Line Alignment:** Qubit structs are aggressively padded to exactly 64 bytes to perfectly match CPU L1 cache lines, eliminating False Sharing and cache invalidation thrashing.
* **Lock-Free Concurrency:** Implements hardware-level `stdatomic.h` Compare-And-Swap (CAS) operations to avoid OS-level mutex overhead.
* **Deadlock Evasion:** Complex multi-qubit entanglements (like 3-way GHZ states and Teleportation) utilize strictly ordered memory-address sorting to prevent the "Dining Philosophers" deadlock under extreme thread contention.
* **Core Affinity:** Utilizes OpenMP `proc_bind(spread)` to bypass OS thread scheduling and pin workloads directly to physical cores for maximum throughput.

## The Architecture: Linear vs. Exponential Scaling
Traditional full-state quantum simulators must track the entire Hilbert space of the system. For a system of `N` qubits, they must calculate and store `2^N` complex amplitudes. Under that model, simulating just 40 qubits requires over a terabyte of RAM, creating a massive "exponential memory wall."

**QuantumBurn** completely bypasses the exponential memory wall by using a flat, localized node-graph architecture. 
* **O(N) Linear Growth:** Each qubit is represented as an independent, 64-byte cache-aligned struct within a 1D array. Adding a new qubit to the simulation costs exactly 64 bytes of RAM, regardless of how large the system is. 
* **Localized Entanglement:** Instead of calculating a massive global tensor product, entanglement and decoherence are simulated using high-speed atomic locks directly between the specific interacting structs. 

This localized approach is what allows QuantumBurn to successfully process quantum phenomena—like BB84 encryption drops and massive GHZ triplet states—across arrays of 1,000,000+ qubits on standard consumer hardware without triggering out-of-memory (OOM) fatal errors.

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
