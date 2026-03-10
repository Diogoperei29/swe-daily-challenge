#include <cstdint>
// Encrypt plaintext with AES-128-GCM.
// key:    16 bytes  |  nonce: 12 bytes (96-bit IV)
// aad / aad_len: additional authenticated data (may be 0 / nullptr)
// ciphertext: caller-allocated buffer >= plaintext_len bytes
// tag_out: 16-byte authentication tag written here
// Returns 0 on success, -1 on any EVP error.
int aes_gcm_encrypt(const uint8_t *key,   const uint8_t *nonce,
                    const uint8_t *aad,   size_t aad_len,
                    const uint8_t *plaintext,  size_t plaintext_len,
                    uint8_t *ciphertext,  uint8_t *tag_out);

// Decrypt and authenticate. Returns 0 on success, -1 if tag fails or EVP error.
int aes_gcm_decrypt(const uint8_t *key,   const uint8_t *nonce,
                    const uint8_t *aad,   size_t aad_len,
                    const uint8_t *ciphertext, size_t ciphertext_len,
                    const uint8_t *tag_in,
                    uint8_t *plaintext_out);