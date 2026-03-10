
## Challenge [003] — AES-GCM: Implement AEAD and Prove Nonce Reuse Is Fatal

### Language
C++ (C++17). Compile with `g++ -std=c++17 -Wall -Wextra -lssl -lcrypto`. OpenSSL ≥ 1.1 must be available on the build host.

### Description
AES-GCM is the AEAD cipher mode underpinning TLS 1.3, automotive Secure Boot, SecOC message authentication, and practically every modern security protocol you will encounter. It delivers both confidentiality (via an internal AES-CTR keystream) and authenticity (via a GHASH polynomial MAC). Your task is to wire up the OpenSSL EVP AEAD API to produce two clean functions with the following signatures and place them in `aes_gcm.cpp` / `aes_gcm.h`:

```cpp
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
```

Once both functions are working, write the second half of `main.cpp`: a **nonce-reuse attack demonstration**. Encrypt two *different* plaintexts M1 and M2 with the **same key and nonce**. You now have ciphertexts C1 and C2. An attacker who knows M1 in plaintext (a common real-world scenario: known-plaintext, or one decrypted session) can compute `C1 ⊕ C2 ⊕ M1` to recover M2 completely — no brute force, no decryption key needed. Print the recovered M2 and assert it matches the original. This is a deterministic algebraic recovery, not a guess.

The AAD parameter must be genuinely exercised: include at least one encrypt/decrypt cycle with a non-empty AAD string (e.g., a CAN frame header, a vehicle VIN, a sequence number). Then demonstrate that flipping a single byte of the AAD at decrypt time causes `aes_gcm_decrypt` to return `-1` (tag verification failure), and that the output buffer is zeroed or untouched.

### Research Guidance
- `man EVP_EncryptInit_ex` and the OpenSSL wiki "EVP Authenticated Encryption and Decryption" page — pay close attention to `EVP_CTRL_GCM_SET_IVLEN`, `EVP_CTRL_GCM_GET_TAG`, and `EVP_CTRL_GCM_SET_TAG`
- RFC 5116 §2 — the generic AEAD interface; understand what the "nonce" uniqueness requirement actually means and what the spec says happens if it is violated
- GCM internals: the keystream is `AES_CTR(key, nonce, counter)` — this is why C1 ⊕ C2 = M1 ⊕ M2 and why knowing one plaintext gives you the other
- "Nonce misuse-resistant AEAD" — search this phrase; understand why AES-SIV (RFC 5297) and AES-GCM-SIV were invented and what property they add
- Joux forbidden attack on GCM (nonce reuse + GHASH key recovery) — you will not implement this, but reading the summary explains why nonce reuse breaks authenticity too, not just confidentiality

### Topics Covered
AES-GCM, AEAD, authenticated encryption, nonce reuse attack, OpenSSL EVP API, `EVP_CIPHER_CTX`, GCM keystream XOR recovery, additional authenticated data (AAD), RFC 5116

### Completion Criteria
1. `aes_gcm_encrypt` / `aes_gcm_decrypt` correctly round-trip for at least three message lengths: 0 bytes, 16 bytes (one full AES block), and 47 bytes (a non-block-aligned length).
2. Decryption returns `-1` and does not produce valid output when: (a) one ciphertext byte is flipped, (b) the tag is modified, and (c) the AAD is modified — all three cases are demonstrated separately in `main.cpp`.
3. The nonce-reuse section encrypts M1 and M2 under the same key and nonce, computes `C1 ⊕ C2 ⊕ M1`, and prints a line confirming the recovered plaintext equals M2.
4. A non-empty AAD is used in at least one round-trip test (criterion 1 or a separate test).
5. No deprecated OpenSSL API (`EVP_EncryptInit` without `_ex`, `EVP_CIPHER_CTX_new` is fine) — only the `EVP_CIPHER_CTX`-based modern interface.
6. The project compiles with `-Wall -Wextra` and zero warnings.

### Difficulty Estimate
~20 min (knowing OpenSSL EVP + GCM internals) | ~60 min (researching from scratch)

### Category
Security & Exploitation
