// build: g++ main.cpp aes_gcm.cpp -o main -std=c++17 -Wall -Wextra -lssl -lcrypto
// needs openssl installed

#include "aes_gcm.h"
#include <iostream>
#include <vector>
#include <cstring>


enum class forced_issue {
    NORMAL_TRIP,
    NORMAL_TRIP_WITH_AAD,
    CHANGE_CIPHER,
    CHANGE_TAG,
    CHANGE_AAD
};

bool round_trip(const uint8_t *key, const uint8_t *iv,
                const uint8_t *pt, size_t pt_len, forced_issue issue) {
    std::vector<uint8_t> ct(pt_len);
    uint8_t tag[16];
    const uint8_t aad[8] = { 0xAA, 0xBB, 0xCC, 0xDD };
    
    int ct_len;
    if (issue ==  forced_issue::NORMAL_TRIP_WITH_AAD) {
        ct_len = aes_gcm_encrypt(key, iv, aad, 8, pt, pt_len, ct.data(), tag);
    } else {
        ct_len = aes_gcm_encrypt(key, iv, nullptr, 0, pt, pt_len, ct.data(), tag);
    }
    if (ct_len < 0) {
        std::cerr << "encrypt failed\n";
        return false;
    }

    std::vector<uint8_t> out(pt_len);
    int pt_len_out;
    if (issue == forced_issue::CHANGE_CIPHER) {
            ct[0] = 0x0;
    } else if (issue ==  forced_issue::CHANGE_TAG) {
            tag[0] = 0x1;
    } 
    
    if (issue ==  forced_issue::NORMAL_TRIP_WITH_AAD) {
        pt_len_out = aes_gcm_decrypt(key, iv, aad, 8, ct.data(), ct_len, tag, out.data());
    } else if (issue ==  forced_issue::CHANGE_AAD) {
        const uint8_t wrong_aad[1] = { 0x1 };
        pt_len_out = aes_gcm_decrypt(key, iv, wrong_aad, 1, ct.data(), ct_len, tag, out.data());
    } else {
        pt_len_out = aes_gcm_decrypt(key, iv, nullptr, 0, ct.data(), ct_len, tag, out.data());
    }

    if (pt_len_out < 0) {
        std::cerr << "decrypt/auth failed\n";
        return false;
    }

    if (static_cast<size_t>(pt_len_out) != pt_len) {
        std::cerr << "length mismatch\n";
        return false;
    }

    return (std::memcmp(pt, out.data(), pt_len) == 0);
}

int main() {
    const uint8_t key[32] = { 0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
                              0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
                              0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
                              0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 };
    const uint8_t iv[12] = { 0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
                             0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88 };

    const uint8_t msg0[0] = {};
    const uint8_t msg1[16] = { 0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
                               0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97 };
    const uint8_t msg2[47] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                               0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
                               0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
                               0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
                               0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
                               0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52 };

    std::cout << "msg0 round-trip: " << (round_trip(key, iv, msg0, sizeof(msg0), forced_issue::NORMAL_TRIP) ? "OK" : "FAIL") << "\n";
    std::cout << "msg1 round-trip: " << (round_trip(key, iv, msg1, sizeof(msg1), forced_issue::NORMAL_TRIP) ? "OK" : "FAIL") << "\n";
    std::cout << "msg2 round-trip: " << (round_trip(key, iv, msg2, sizeof(msg2), forced_issue::NORMAL_TRIP) ? "OK" : "FAIL") << "\n";
    std::cout << "msg2 round-trip (WRONG CYPHERTEXT - FAIL): " << (round_trip(key, iv, msg2, sizeof(msg2), forced_issue::CHANGE_CIPHER) ? "OK" : "FAIL") << "\n";
    std::cout << "msg2 round-trip (WRONG TAG - FAIL): " << (round_trip(key, iv, msg2, sizeof(msg2), forced_issue::CHANGE_TAG) ? "OK" : "FAIL") << "\n";
    std::cout << "msg2 round-trip (WRONG AAD - FAIL): " << (round_trip(key, iv, msg2, sizeof(msg2), forced_issue::CHANGE_AAD) ? "OK" : "FAIL") << "\n";
    std::cout << "msg2 round-trip (WITH AAD - OK): " << (round_trip(key, iv, msg2, sizeof(msg2), forced_issue::NORMAL_TRIP_WITH_AAD) ? "OK" : "FAIL") << "\n";


    // obtain IV with nonce reuse
    const uint8_t msg4[16] = { 0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
                               0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97 };
    const uint8_t msg5[16] = { 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
                               0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60 };
    std::vector<uint8_t> ct1(16);
    std::vector<uint8_t> ct2(16);
    std::vector<uint8_t> recovered(16);
    uint8_t tag[16];
    (void) aes_gcm_encrypt(key, iv, nullptr, 0, msg4, sizeof(msg4), ct1.data(), tag);
    (void) aes_gcm_encrypt(key, iv, nullptr, 0, msg5, sizeof(msg5), ct2.data(), tag);

    // M2 = C1 ⊕ C2 ⊕ M1
    for(int i = 0; i < 16; ++i)
    {
        recovered[i] = ct1[i] ^ ct2[i] ^ msg4[i];
    }

    std::cout << "nonce reuse recovered: " << ((std::memcmp(recovered.data(), msg5, 16) == 0) ? "YES" : "NO") << "\n";
    return 0;
}