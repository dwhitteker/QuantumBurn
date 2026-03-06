#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <stdint.h>
#include <stdatomic.h>

// The 40-bit Semiprime Target to factor
#define TARGET_N 14254051285619ULL//1000036000099ULL 
#define MAX_GUESSES 10000 

// Global flag to stop all threads once the factors are found
atomic_int target_factored = 0;

static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

// Classical Greatest Common Divisor (Euclidean Algorithm)
unsigned long long gcd(unsigned long long a, unsigned long long b) {
    while (b != 0) {
        unsigned long long temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

// Simulates the Quantum Period Finding subroutine
// This is what the Quantum Fourier Transform does instantly!
unsigned long long find_period(unsigned long long a, unsigned long long N) {
    unsigned __int128 val = 1;
    unsigned long long r = 0;
    
    // Brute-force the repeating wave: a^r mod N == 1
    while (!atomic_load(&target_factored)) {
        r++;
        val = (val * a) % N;
        if (val == 1) {
            return r; // Period found!
        }
        // Failsafe to prevent infinite loops on bad guesses
        if (r > N) return 0; 
    }
    return 0; // Aborted by another thread
}

// Fast Modular Exponentiation: (base^exp) % mod
unsigned long long mod_pow(unsigned long long base, unsigned long long exp, unsigned long long mod) {
    unsigned __int128 res = 1;
    unsigned __int128 b = base % mod;
    while (exp > 0) {
        if (exp % 2 == 1) res = (res * b) % mod;
        b = (b * b) % mod;
        exp /= 2;
    }
    return (unsigned long long)res;
}

int main() {
    // Release the hounds!
    omp_set_dynamic(0); 

    printf("--- SHOR'S ALGORITHM SIMULATION | 64 THREADS ---\n");
    printf("TARGET RSA SEMIPRIME : %llu\n", TARGET_N);
    printf("--- HUNTING FOR THE QUANTUM PERIOD... ---\n\n");
    
    double start_time = omp_get_wtime();

    #pragma omp parallel num_threads(64) proc_bind(spread)
    {
        uint32_t seed = (omp_get_thread_num() + 1) * 0x9E3779B9 ^ (uint32_t)time(NULL);

        // Dynamic schedule ensures threads immediately grab a new guess if their previous one fails
        #pragma omp for schedule(dynamic, 1)
        for(int i = 0; i < MAX_GUESSES; i++) {
            
            // If another core cracked it, drop everything and exit
            if (atomic_load(&target_factored)) continue;

            // 1. Pick a random quantum state 'a' between 2 and N-1
            unsigned long long a = 2 + (xorshift32(&seed) % (TARGET_N - 3));
            
            // Quick check: Did we randomly guess the prime factor? (Extremely rare)
            unsigned long long initial_check = gcd(a, TARGET_N);
            if (initial_check > 1 && initial_check < TARGET_N) {
                if (atomic_exchange(&target_factored, 1) == 0) {
                    printf("[THREAD %02d] INCREDIBLE LUCK! Guessed factor directly: %llu\n", omp_get_thread_num(), initial_check);
                }
                continue;
            }

            // 2. The heavy lifting: Find the period 'r'
            unsigned long long r = find_period(a, TARGET_N);

            // 3. Shor's mathematical requirement: the period 'r' must be an even number
            if (r > 0 && r % 2 == 0) {
                
                // Calculate x = a^(r/2) mod N
                unsigned long long x = mod_pow(a, r / 2, TARGET_N);
                
                // Check against trivial factors
                if (x != 1 && x != TARGET_N - 1) {
                    
                    // 4. Extract the Primes! 
                    unsigned long long p = gcd(x + 1, TARGET_N);
                    unsigned long long q = gcd(x - 1, TARGET_N);
                    
                    if (p * q == TARGET_N) {
                        // We found it! Lock the flag so the other 63 threads stop.
                        if (atomic_exchange(&target_factored, 1) == 0) {
                            double end_time = omp_get_wtime();
                            printf("\n========== CRACKED! ==========\n");
                            printf("Thread ID       : %d\n", omp_get_thread_num());
                            printf("Quantum Base (a): %llu\n", a);
                            printf("Wave Period (r) : %llu\n", r);
                            printf("Prime Factor P  : %llu\n", p);
                            printf("Prime Factor Q  : %llu\n", q);
                            printf("Verification    : %llu * %llu = %llu\n", p, q, p * q);
                            printf("Execution Time  : %.4f seconds\n", end_time - start_time);
                            printf("==============================\n");
                        }
                    }
                }
            }
        }
    }

    if (!atomic_load(&target_factored)) {
        printf("\nSimulation failed to factor the target within %d guesses.\n", MAX_GUESSES);
    }

    return 0;
}
