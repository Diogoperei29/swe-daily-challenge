// compile: g++ -std=c++17 -O2 -Wall -Wextra -o bloom main.cpp && ./bloom
#include "bloom.h"
#include <cassert>
#include <cstdio>
#include <string>

// Generate URLs as strings: "url-0", "url-1", ...
// Offset the range so training vs probe sets are disjoint.
std::string make_url(size_t i) { return "url-" + std::to_string(i); }

int main() {
    const size_t N          = 100'000;
    const double FPR_TARGET = 0.01; // 1%

    BloomFilter bf(N, FPR_TARGET);

    printf("m=%zu bits (%.2f KiB), k=%zu hash functions\n",
           bf.bit_count(),
           bf.bit_count() / 8.0 / 1024.0,
           bf.hash_count());

    // Insert N URLs
    for (size_t i = 0; i < N; i++)
        bf.insert(make_url(i));

    // 1. No false negatives: every inserted URL must be found
    for (size_t i = 0; i < N; i++)
        assert(bf.contains(make_url(i)) && "false negative — must never happen");

    puts("No false negatives: PASS");

    // 2. Measure empirical FPR on unseen URLs (offset by N to avoid overlap)
    size_t fp = 0;
    const size_t PROBE = 200'000;
    for (size_t i = N; i < N + PROBE; i++)
        if (bf.contains(make_url(i)))
            ++fp;

    double empirical_fpr = static_cast<double>(fp) / PROBE;
    double theory_fpr    = bf.theoretical_fpr();

    printf("Theoretical FPR : %.4f%%\n", theory_fpr    * 100.0);
    printf("Empirical   FPR : %.4f%%\n", empirical_fpr * 100.0);

    // 3. Empirical FPR must be within 0.5% of theoretical
    assert(std::abs(empirical_fpr - theory_fpr) < 0.005 &&
           "empirical FPR deviates too much from theoretical");

    puts("FPR within tolerance: PASS");

    // 4. Empirical FPR must not exceed 2x the configured target
    assert(empirical_fpr <= FPR_TARGET * 2.0 &&
           "FPR exceeded 2x the configured target");

    puts("FPR within 2x target: PASS");

    return 0;
}
