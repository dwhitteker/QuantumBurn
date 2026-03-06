#!/bin/bash

# ==============================================================================
# 64-THREAD QUANTUM SIMULATION STRESS TEST SUITE
# ==============================================================================

# Array of the C files we built tonight
SIMULATIONS=(
    "25b_2d_grid.c"         # Entanglement & Decoherence (Monogamy)
    "10b_ghz_triplet.c"     # GHZ Triplet State (3-way locks)
    "10b_teleport.c"        # Quantum Teleportation (No-Cloning)
    "25b_qec.c"             # Quantum Error Correction (Bit-flip)
    "shors_sim.c"           # Shor's Algorithm (Period finding race)
    "grover.c"              # Grover's Algorithm (Amplitude Amplification)
    "grover_db.c"           # Grover's Database Search (Dynamic QRAM)
    "bb84_sim.c"            # BB84 Quantum Key Distribution
    "25b_qec_distance7.c"   # Quantum Error Correction (distance 7)
    "2d_surface_code.c"	    # Simulates Google/IBM's topological architecture
)

# Compilation flags for maximum hardware utilization
COMPILER="gcc"
FLAGS="-O3 -march=native -ffast-math -fopenmp"
LIBS="-lm"
OUTPUT_BIN="quantum_burn"

echo "====================================================================="
echo "  INITIATING QUANTUM CPU STRESS TEST SUITE"
echo "  Hardware Target: 64 Threads | Max ALU/FPU Utilization"
echo "====================================================================="
echo ""

# Record total suite start time
SUITE_START=$(date +%s)
PASSED=0
SKIPPED=0

for FILE in "${SIMULATIONS[@]}"; do
    echo "---------------------------------------------------------------------"
    
    if [ ! -f "$FILE" ]; then
        echo "[SKIPPED] Cannot find $FILE in the current directory."
        ((SKIPPED++))
        continue
    fi
    
    echo "[BUILDING] Compiling $FILE..."
    $COMPILER $FLAGS "$FILE" -o $OUTPUT_BIN $LIBS
    
    if [ $? -ne 0 ]; then
        echo "[ERROR] Compilation failed for $FILE. Halting suite."
        exit 1
    fi
    
    echo "[RUNNING] Executing $FILE (Watch btop!)..."
    echo "---------------------------------------------------------------------"
    
    # Run the compiled binary
    ./$OUTPUT_BIN
    
    echo "---------------------------------------------------------------------"
    echo "[DONE] $FILE completed."
    echo ""
    ((PASSED++))
    
    # Give the CPU a tiny 2-second breather to clear caches between heavy burns
    sleep 2
done

# Clean up the compiled binary
if [ -f "$OUTPUT_BIN" ]; then
    rm $OUTPUT_BIN
fi

SUITE_END=$(date +%s)
TOTAL_TIME=$((SUITE_END - SUITE_START))

echo "====================================================================="
echo "  QUANTUM SUITE COMPLETE"
echo "====================================================================="
echo "Simulations Run : $PASSED"
echo "Files Skipped   : $SKIPPED"
echo "Total Wall Time : $TOTAL_TIME seconds"
echo "====================================================================="
