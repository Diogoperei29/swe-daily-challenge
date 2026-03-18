// Compile: g++ -std=c++17 -Wall -Wextra -Wno-deprecated-declarations -o main main.cpp -lssl -lcrypto
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <openssl/sha.h>
#include <openssl/hmac.h>

// -----------------------------------------------------------------------
// The "server" side — this is the code you may NOT change for Part A.
// For Part B you will replace naive_mac with hmac_sha256.
// -----------------------------------------------------------------------

constexpr size_t KEY_LEN = 16;
// Pretend this is a secret only the server knows.
static const uint8_t SECRET_KEY[KEY_LEN] = {
    0x0f,0x1e,0x2d,0x3c,0x4b,0x5a,0x69,0x78,
    0x87,0x96,0xa5,0xb4,0xc3,0xd2,0xe1,0xf0
};

// Naive MAC: SHA256(key || msg)  <-- VULNERABLE
std::vector<uint8_t> naive_mac(const uint8_t* msg, size_t msg_len) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, SECRET_KEY, KEY_LEN);
    SHA256_Update(&ctx, msg, msg_len);
    std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
    SHA256_Final(digest.data(), &ctx);
    return digest;
}

bool server_verify_naive(const uint8_t* msg, size_t msg_len,
                         const std::vector<uint8_t>& token) {
    return naive_mac(msg, msg_len) == token;
}

// -----------------------------------------------------------------------
// HMAC-SHA256 MAC  (implement this for Part B)
// -----------------------------------------------------------------------
std::vector<uint8_t> hmac_sha256_mac(const uint8_t* msg, size_t msg_len) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    unsigned int out_len = 0;
    // production should use CRYPTO_memcmp() instead of ==
    HMAC(EVP_sha256(), SECRET_KEY, KEY_LEN, msg, msg_len, out.data(), &out_len);
    return out;
}

bool server_verify_hmac(const uint8_t* msg, size_t msg_len,
                        const std::vector<uint8_t>& token) {
    return hmac_sha256_mac(msg, msg_len) == token;
}

// -----------------------------------------------------------------------
// ATTACKER — implement the length extension attack here
// -----------------------------------------------------------------------

// Given a valid (original_msg, valid_token) pair, return:
//   {extended_msg, forged_token}
// where extended_msg = original_msg || sha256_padding(KEY_LEN + original_msg.size()) || extension
// and forged_token is a valid SHA256(key || extended_msg) without knowing key.
std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
length_extension_attack(const std::vector<uint8_t>& original_msg,
                        const std::vector<uint8_t>& valid_token,
                        const std::vector<uint8_t>& extension) {
    
    uint32_t state[8];
    for (int i = 0; i < 8; i++) {
        state[i] = (static_cast<uint32_t>(valid_token[i*4])   << 24)
                 | (static_cast<uint32_t>(valid_token[i*4+1]) << 16)
                 | (static_cast<uint32_t>(valid_token[i*4+2]) <<  8)
                 |  static_cast<uint32_t>(valid_token[i*4+3]);
    }

    uint32_t length = KEY_LEN + original_msg.size();
    std::vector<uint8_t> padding_1;
    padding_1.push_back(0x80);
    while((length + padding_1.size()) % 64 != 56) padding_1.push_back(0x00);
    for (int i = 7; i >= 0; --i) {
        padding_1.push_back(static_cast<uint8_t>(static_cast<uint64_t>(length * 8) >> (i * 8)));
    }

    // extended_msg = msg || padding_1 || "&admin=1"
    std::vector<uint8_t> extended_msg = original_msg;
    extended_msg.insert(extended_msg.end(), padding_1.begin(), padding_1.end());
    extended_msg.insert(extended_msg.end(), extension.begin(), extension.end());

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    for (int i = 0; i < 8; ++i)
        ctx.h[i] = state[i];
    ctx.Nl = static_cast<uint32_t>((KEY_LEN + original_msg.size() + padding_1.size()) * 8);
    ctx.Nh = 0;
    ctx.num = 0;  // no partial block buffered

    SHA256_Update(&ctx, extension.data(), extension.size());
    std::vector<uint8_t> forged_token(SHA256_DIGEST_LENGTH); 
    SHA256_Final(forged_token.data(), &ctx);

    return {extended_msg, forged_token};
}

// -----------------------------------------------------------------------
// main: wire it all together and print PASS/FAIL for each check
// -----------------------------------------------------------------------
int main() {
    // Original legitimate request
    std::vector<uint8_t> msg;
    std::string cmd = "command=reboot";
    msg.assign(cmd.begin(), cmd.end());

    auto token = naive_mac(msg.data(), msg.size());

    // --- Part A ---
    std::vector<uint8_t> extension;
    std::string ext_str = "&admin=1";
    extension.assign(ext_str.begin(), ext_str.end());

    auto [ext_msg, forged_token] = length_extension_attack(msg, token, extension);

    bool part_a = server_verify_naive(ext_msg.data(), ext_msg.size(), forged_token);
    std::cout << "Part A (length extension forgery): "
              << (part_a ? "PASS — server accepted forged token" : "FAIL") << "\n";

    // --- Part B ---
    auto hmac_token = hmac_sha256_mac(msg.data(), msg.size());
    bool part_b = !server_verify_hmac(ext_msg.data(), ext_msg.size(), forged_token);
    std::cout << "Part B (HMAC rejects forgery):      "
              << (part_b ? "PASS — server rejected forged token" : "FAIL") << "\n";

    return (part_a && part_b) ? 0 : 1;
}