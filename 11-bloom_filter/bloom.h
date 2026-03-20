#pragma once
#include <cstdint>
#include <cmath>
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
    BloomFilter(size_t expected_n, double fpr) {
        m_ = static_cast<size_t>(std::ceil(-(double)expected_n * std::log(fpr) / (std::log(2) * std::log(2))));
        k_ = static_cast<size_t>(std::round((double)m_ / expected_n * std::log(2)));
        if (k_ < 1) k_ = 1;
        n_inserted_ = 0;
        bits_.assign(m_, false);
    }

    void insert(const std::string& key) {
        for(size_t i = 0; i < k_; i++) {
            size_t g = hash_i(key, i);
            bits_[g] = true;
        }
        ++n_inserted_;
    }

    // may return false positives
    bool contains(const std::string& key) const{
        for(size_t i = 0; i < k_; i++) {
            size_t g = hash_i(key, i);
            if (bits_[g] == false) return false;
        }
        return true;
    } 

    size_t bit_count()    const { return m_; }
    size_t hash_count()   const { return k_; }
    size_t insert_count() const { return n_inserted_; }

    // Returns the theoretical FPR given current state: (1 - e^(-k*n/m))^k
    double theoretical_fpr() const {
        double q = std::exp(-(double)k_ * n_inserted_ / m_);
        return std::pow(1.0 - q, k_);
    }

private:
    size_t            m_;           // number of bits
    size_t            k_;           // number of hash functions
    size_t            n_inserted_;  // elements inserted so far
    std::vector<bool> bits_;

    // TODO: implement — returns the i-th hash position for key (mod m)
    // g_i(x) = (h1(x) + i · h2(x))  mod m,   for i = 0, 1, ..., k−1
    size_t hash_i(const std::string& key, size_t i) const {
        uint64_t a = fnv1a_64(key);
        uint64_t b = djb2_64(key);
        // Apply MurmurHash3 finalizer for better avalanche before use
        a ^= a >> 33; a *= 0xff51afd7ed558ccdULL; a ^= a >> 33;
        b ^= b >> 33; b *= 0xc4ceb9fe1a85ec53ULL; b ^= b >> 33;
        return static_cast<size_t>((a + i * b) % m_);
    }
};
