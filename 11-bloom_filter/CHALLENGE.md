## Challenge [011] — Bloom Filter: Probabilistic Set Membership

### Language
C++ (C++17 or later)

### Description
A junior engineer on your team has been tasked with building a cache for a
URL reputation service — before hitting an expensive database, the service
should quickly determine "has this URL definitely never been seen before?"
If it has, we do a full DB lookup. If not, we skip it. A small rate of false
positives (incorrectly saying "maybe seen it" for a URL we haven't) is
tolerable; false negatives (saying "never seen it" for a URL we have) are not.

They've heard "Bloom filter" is the right tool, but they're not sure how to
implement one, how to choose parameters, or how accurate it will actually be.
Your job is to implement a correct, parameterized Bloom filter and then
**empirically validate** that the measured false positive rate matches the
theoretical prediction.

You are given the following scaffold. Fill in the two stubbed methods in
`bloom.h`, implement the helper methods, and make `main.cpp` pass all
its assertions.

**`bloom.h` — scaffold:**
```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

// Kirsch-Mitzenmacher double-hashing trick:
//   g_i(x) = h1(x) + i * h2(x)  (mod m)
// Only two base hashes are needed to simulate k independent hash functions.

inline uint64_t fnv1a_64(const std::string& s) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline uint64_t djb2_64(const std::string& s) {
    uint64_t hash = 5381;
    for (unsigned char c : s)
        hash = ((hash << 5) + hash) ^ c;
    return hash;
}

class BloomFilter {
public:
    // Construct a Bloom filter for 'expected_n' elements
    // with a target false-positive rate of 'fpr' (e.g., 0.01 = 1%).
    // Derive m and k from these two parameters.
    BloomFilter(size_t expected_n, double fpr);

    void   insert(const std::string& key);
    bool   contains(const std::string& key) const; // may return false positives

    size_t bit_count()   const { return m_; }
    size_t hash_count()  const { return k_; }
    size_t insert_count() const { return n_inserted_; }

    // Returns the theoretical FPR given current state: (1 - e^(-k*n/m))^k
    double theoretical_fpr() const;

private:
    size_t              m_;          // number of bits
    size_t              k_;          // number of hash functions
    size_t              n_inserted_; // elements inserted so far
    std::vector<bool>   bits_;

    // TODO: implement — returns the i-th hash position for key (mod m)
    size_t hash_i(const std::string& key, size_t i) const;
};
```

**`main.cpp` — scaffold:**
```cpp
#include "bloom.h"
#include <cassert>
#include <cstdio>
#include <string>

// Generate 'count' fake URLs as strings: "url-0", "url-1", ...
// starting from 'offset' so we can produce disjoint training vs test sets.
std::string make_url(size_t i) { return "url-" + std::to_string(i); }

int main() {
    const size_t N  = 100'000;  // elements to insert
    const double FPR_TARGET = 0.01; // 1% target

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

    // 2. Measure empirical FPR on N unseen URLs (offset by N to avoid overlap)
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

    // 4. Empirical FPR must not exceed 2x the target
    assert(empirical_fpr <= FPR_TARGET * 2.0 &&
           "FPR exceeded 2x the configured target");

    puts("FPR within 2x target: PASS");

    return 0;
}
```

Compile with:
```
g++ -std=c++17 -O2 -Wall -Wextra -o bloom main.cpp && ./bloom
```

---

### Background & Teaching

#### Core Concepts

A **Bloom filter** is a space-efficient probabilistic data structure that
answers one question: *"Is this element in the set?"* — with the guarantee
that it will **never produce false negatives** (if the element was inserted,
`contains` always returns `true`), but it **may produce false positives**
(it can say `true` for elements that were never inserted, with some
controllable probability).

**How it works — the mechanics:**

A Bloom filter is a bit array of `m` bits, initially all zero, and `k`
independent hash functions h₁, h₂, …, hₖ that each map a key to a
position in `[0, m)`.

- **Insert(x):** compute h₁(x), h₂(x), …, hₖ(x); set those k bit positions to 1.
- **Contains(x):** compute the same k positions; return `true` if **all** are 1.

Why can false positives happen? Because bits set by other insertions may
coincidentally already cover all k positions of a new query key — even
though that key was never inserted.

Why are false negatives impossible? If x was inserted, every one of its k
positions was set to 1, and bits are never cleared. So `contains(x)` will
always find all k bits set.

**The math — sizing for a target FPR:**

Let:
- `n` = expected number of insertions
- `m` = number of bits in the filter
- `k` = number of hash functions

The probability that a given bit is still 0 after inserting one element is
`(1 − 1/m)`. After `n` insertions across `k` functions, the probability
that a given bit is still 0 is approximately `e^(−kn/m)` (using the
limit of `(1 − 1/m)^(mk)` as m→∞).

Then the false positive probability (all k positions for a new key happen
to already be 1) is:

```
FPR ≈ (1 − e^(−kn/m))^k
```

**Optimal m given n and desired FPR:**

Solving for m:
```
m = −(n · ln(FPR)) / (ln(2))²
```

**Optimal k given m and n:**

```
k = (m / n) · ln(2)
```

Round k to the nearest integer ≥ 1. Plugging in the optimal k gives the
minimum FPR for a fixed m/n ratio, specifically:
```
FPR_optimal ≈ (0.6185)^(m/n)
```

As a concrete example: to hold 100,000 elements at 1% FPR, you need
approximately 958,506 bits (~117 KiB) and k=7 hash functions.

**The Kirsch-Mitzenmacher trick (double hashing):**

Maintaining k truly independent hash functions is cumbersome. The
Kirsch-Mitzenmacher 2006 paper proved that you can simulate k independent
functions using only *two* independent hash functions h₁ and h₂:

```
g_i(x) = (h1(x) + i · h2(x))  mod m,   for i = 0, 1, ..., k−1
```

This has the same asymptotic FPR as using k independent functions, with
zero performance penalty. In practice, h₁ and h₂ can be FNV-1a and djb2
(or two seeds of MurmurHash).

**Space efficiency:**

The optimal bits-per-element is `m/n = −ln(FPR) / (ln 2)²`. At 1% FPR
this is ~9.59 bits/element. For comparison, a hash set in C++ uses
~64+ bits per element (pointer, hash, value). A Bloom filter is 6–10×
smaller while answering membership queries in O(k) time.

---

#### How to Approach This in C++

**Sizing the filter (`BloomFilter` constructor):**

Derive m and k from `expected_n` and `fpr`:
```cpp
m_ = static_cast<size_t>(std::ceil(-expected_n * std::log(fpr) / (std::log(2) * std::log(2))));
k_ = static_cast<size_t>(std::round((double)m_ / expected_n * std::log(2)));
if (k_ < 1) k_ = 1;
bits_.assign(m_, false);
```

**The bit array:**

`std::vector<bool>` is a specialization that stores one bit per element —
it is exactly the right tool here. It is random-access and correctly
handles `operator[]` for both get and set. You don't need to manage a
`uint8_t` array manually unless you need raw speed.

**The hash functions:**

`fnv1a_64` and `djb2_64` are provided in the scaffold. Both are
64-bit hashes. Implement `hash_i` using the Kirsch-Mitzenmacher formula:
```cpp
size_t hash_i(const std::string& key, size_t i) const {
    uint64_t a = fnv1a_64(key);
    uint64_t b = djb2_64(key);
    return static_cast<size_t>((a + i * b) % m_);
}
```

**`insert` and `contains`:**

Both loop over `i` from 0 to k−1, call `hash_i`, and either set or test
the corresponding bit. `contains` should return `false` as soon as any bit
is 0 (short-circuit).

**`theoretical_fpr`:**

Use the formula `(1 − e^(−k * n / m))^k` where `n = n_inserted_`:
```cpp
double q = std::exp(-(double)k_ * n_inserted_ / m_);
return std::pow(1.0 - q, k_);
```

**Common mistakes to avoid:**

- Using `%` with signed integers — keep everything `size_t` / `uint64_t`.
- Not checking that `fpr` is in `(0, 1)` and `expected_n > 0`.
- Computing `m_` before rounding up with `std::ceil`.
- Forgetting that `std::vector<bool>::operator[]` for write requires
  `bits_[pos] = true`, not a pointer dereference.

---

#### Key Pitfalls & Security/Correctness Gotchas

- **Never return a false negative.** If you clear a bit anywhere — e.g.,
  during a misguided "delete" — you break the fundamental guarantee.
  Standard Bloom filters do not support deletion. (Counting Bloom filters do.)

- **Modular reduction bias.** If you compute `hash % m` and m is not a power
  of 2, there is a slight bias toward smaller values. For very large m this
  is negligible, but if you want perfect uniformity use rejection sampling
  or choose m as a power of 2. For this challenge the bias is acceptable.

- **h₁ and h₂ must be sufficiently independent.** Using two seeds of the
  same hash function (e.g., FNV-1a with different primes) introduces
  correlation. The provided `fnv1a_64` and `djb2_64` come from different
  designs and are sufficiently independent for this use case.

- **`theoretical_fpr()` is only valid after insertions.** Before any
  insertions, `n_inserted_ = 0` and the formula returns 0.0, which is
  the trivially correct answer. Make sure you call it after inserting N
  elements, not before.

- **Empirical FPR variance.** For small PROBE counts the measured FPR
  can deviate significantly from theory. With PROBE = 200,000 the
  central limit theorem makes the estimate stable. Do not reduce PROBE
  in testing or the tolerance assertion may flap.

- **Integer overflow in `a + i * b`.** For large `k` and 64-bit hash values,
  `i * b` can overflow. Since C++ unsigned arithmetic wraps modulo 2⁶⁴,
  this is intentional and safe — the result still distributes well.

---

#### Bonus Curiosity (Optional — if you finish early)

- **Counting Bloom filters:** Replace each bit with a small integer counter
  (typically 4 bits). Increment on insert, decrement on delete. This
  enables deletion at the cost of 4× memory. The tradeoff is subtle:
  counter overflow (after ~16 insertions of the same key at one position)
  causes a permanent false positive. Look up the 1998 Fan et al. paper.

- **Cuckoo filters:** A newer (2014, Fan et al.) alternative that supports
  deletion, uses less memory than a counting Bloom filter, and achieves
  better FPR at high load. Their trick is to store a small fingerprint in
  one of two candidate buckets, using cuckoo hashing to resolve conflicts.
  Practically used in Redis, ClickHouse, and Guava.

- **HyperLogLog:** A completely different probabilistic structure that
  answers "how many distinct elements have been inserted?" (cardinality)
  in O(1) memory. Used in Redis's `PFCOUNT`. The underlying insight — using
  the position of the leading zero in a hash as a log₂(n) estimator — is
  one of the most elegant tricks in applied computer science.

---

### Research Guidance

- `man 3 bitset` and cppreference on `std::vector<bool>` — understand the
  proxy reference returned by `operator[]` for `vector<bool>` and why it
  differs from `vector<int>`.

- Kirsch & Mitzenmacher, 2006: *"Less Hashing, Same Performance: Building
  a Better Bloom Filter"* — the proof that double hashing works as well as
  k independent functions, with the exact FPR formula.

- Broder & Mitzenmacher, 2004: *"Network Applications of Bloom Filters:
  A Survey"* — practical applications in routing tables, web crawling, and
  CDN cache digests.

- FNV hash specification at http://www.isthe.com/chongo/tech/comp/fnv/ —
  understand why the specific prime and offset basis were chosen, and the
  difference between FNV-1 and FNV-1a.

- The Redis source `src/hyperloglog.c` — an excellent real-world example
  of a probabilistic data structure implemented in production C. Compare
  its density tricks to the Bloom filter approach.

---

### Topics Covered
Bloom filter, probabilistic data structures, double hashing (Kirsch-Mitzenmacher), FNV-1a, djb2, false positive rate, optimal m/k derivation, `std::vector<bool>`, empirical vs theoretical analysis

### Completion Criteria
1. `bloom.cpp` / `bloom.h` compiles with `-std=c++17 -Wall -Wextra` and zero warnings.
2. Running `./bloom` prints the computed `m` and `k` values for N=100,000, FPR=1%.
3. The "No false negatives" assertion passes for all 100,000 inserted URLs.
4. The empirical FPR measured over 200,000 unseen URLs is within ±0.5 percentage points of the theoretical FPR.
5. The empirical FPR does not exceed 2× the configured target (≤ 2%).
6. `theoretical_fpr()` returns a value computed from the actual number of insertions, not a hardcoded constant.

### Difficulty Estimate
~20 min (knowing the domain) | ~60 min (researching from scratch)

### Category
DATA STRUCTURES & ALGORITHMS
