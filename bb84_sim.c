#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <time.h>
#include <stdint.h>

// 10 Million Photons fired over the fiber optic cable
#define NUM_PHOTONS 10000000 

// Eve intercepts exactly half the transmission
#define EVE_INTERCEPT_RATE 0.50f 

// Arrays to hold the quantum states and classical choices
uint8_t* alice_bits;
uint8_t* alice_bases;
uint8_t* eve_bases;
uint8_t* eve_intercepted;
uint8_t* bob_bases;
uint8_t* bob_results;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

int main() {
    omp_set_dynamic(0);
    
    // Allocate memory for the key distribution
    alice_bits      = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);
    alice_bases     = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);
    eve_bases       = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);
    eve_intercepted = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);
    bob_bases       = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);
    bob_results     = (uint8_t*)aligned_alloc(64, NUM_PHOTONS);

    printf("--- BB84 QUANTUM KEY DISTRIBUTION | 64 THREADS ---\n");
    printf("Photons Transmitted : %d\n", NUM_PHOTONS);
    printf("Eve Intercept Rate  : %.0f%%\n\n", EVE_INTERCEPT_RATE * 100.0f);
    
    double start_time = omp_get_wtime();

    // 1. ALICE SENDS THE PHOTONS
    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);
        const float UINT32_MAX_INV = 1.0f / 4294967295.0f; 

        #pragma omp for schedule(static, 10000)
        for (int i = 0; i < NUM_PHOTONS; i++) {
            // Alice prepares a random bit and a random base (0: +, 1: x)
            alice_bits[i] = xorshift32(&seed) & 1;
            alice_bases[i] = xorshift32(&seed) & 1;
            
            // 2. EVE INTERCEPTS THE FIBER OPTIC CABLE
            if ((float)xorshift32(&seed) * UINT32_MAX_INV < EVE_INTERCEPT_RATE) {
                eve_intercepted[i] = 1;
                eve_bases[i] = xorshift32(&seed) & 1; // Eve guesses a base
            } else {
                eve_intercepted[i] = 0;
            }

            // 3. BOB RECEIVES AND MEASURES
            bob_bases[i] = xorshift32(&seed) & 1; // Bob guesses a base
            
            // Physics Simulation: Collapse the wave based on base mismatches
            uint8_t photon_bit = alice_bits[i];
            uint8_t photon_base = alice_bases[i];

            // If Eve interfered and guessed the WRONG base, the photon is irreversibly scrambled
            if (eve_intercepted[i] == 1 && eve_bases[i] != photon_base) {
                photon_bit = xorshift32(&seed) & 1; // Randomizes state due to Measurement Problem
                photon_base = eve_bases[i];         // Photon assumes Eve's new base
            }

            // Bob measures the incoming photon
            if (bob_bases[i] == photon_base) {
                // Bob guessed right! He reads the photon perfectly.
                bob_results[i] = photon_bit;
            } else {
                // Bob guessed wrong! Physics scrambles the result.
                bob_results[i] = xorshift32(&seed) & 1;
            }
        }
    }

    // 4. SIFTING THE KEY (Alice and Bob compare bases publicly)
    int sifted_key_length = 0;
    int error_count = 0;

    // We do this serially since we are condensing the array, but it takes fractions of a millisecond
    for (int i = 0; i < NUM_PHOTONS; i++) {
        // Did Alice and Bob use the same base?
        if (alice_bases[i] == bob_bases[i]) {
            sifted_key_length++;
            
            // In a perfect world, if bases match, the bits MUST match.
            // If they don't, Eve caused a quantum collapse!
            if (alice_bits[i] != bob_results[i]) {
                error_count++;
            }
        }
    }

    double end_time = omp_get_wtime();

    // 5. SECURITY ANALYSIS
    float error_rate = ((float)error_count / (float)sifted_key_length) * 100.0f;
    
    printf("========== PROTOCOL COMPLETE ==========\n");
    printf("Raw Photons Fired : %d\n", NUM_PHOTONS);
    printf("Sifted Key Length : %d bits (Useful Key)\n", sifted_key_length);
    printf("Errors Detected   : %d mismatches in the sifted key\n", error_count);
    printf("Quantum Error Rate: %.2f%%\n", error_rate);
    
    // Theoretically, a 50% intercept rate should yield a 12.5% error rate on the sifted key
    if (error_rate > 1.0f) {
        printf("\n[ALERT] EAVESDROPPER DETECTED! Quantum state collapse triggered.\n");
        printf("[ALERT] The key has been compromised. Do not encrypt data!\n");
    } else {
        printf("\n[SECURE] Channel is clear. No eavesdropper detected. Key is safe to use.\n");
    }

    printf("\nExecution Time    : %.4f seconds\n", end_time - start_time);
    printf("=======================================\n");

    free(alice_bits);
    free(alice_bases);
    free(eve_bases);
    free(eve_intercepted);
    free(bob_bases);
    free(bob_results);

    return 0;
}
