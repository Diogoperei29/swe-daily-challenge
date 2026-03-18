## Challenge [010] — HMAC Length Extension: Exploit and Fix a Naive MAC

### Language
C++ (C++17). OpenSSL (`libcrypto`) for SHA-256 primitives.

### Description
Your team is auditing an automotive API gateway that authenticates firmware
commands using the following scheme:

```
token = SHA256(secret_key || command_payload)
```

The server verifies by recomputing the same hash over `(secret_key ||
command_payload)` and comparing. The secret key is **16 bytes**, known only
to the server and the legitimate client. An attacker can observe a valid
`(command_payload, token)` pair on the wire.

Your task is twofold:

**Part A — Exploit:** Without knowing `secret_key`, forge a valid token for
the message `command_payload || <SHA-256 padding for len(key||payload)> ||
"&admin=1"`. That is, demonstrate the length extension attack: given a
valid `(msg, token)` pair, produce a new `(extended_msg, forged_token)` pair
that the server will accept — without ever learning the key.

**Part B — Fix:** Replace the naive `SHA256(key || msg)` construction with
proper **HMAC-SHA256**. Show that the same attack now fails: the forged token
is rejected.

Start from this scaffold:

```cpp
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
    // TODO: implement using HMAC() from openssl/hmac.h
    (void)msg; (void)msg_len;
    return {};
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
    // TODO
    return {};
}

// -----------------------------------------------------------------------
// main: wire it all together and print PASS/FAIL for each check
// -----------------------------------------------------------------------
int main() {
    // Original legitimate request
    std::vector<uint8_t> msg(/* "command=reboot" */);
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
```

Compile with:
```
g++ -std=c++17 -Wall -Wextra -o mac_challenge main.cpp -lssl -lcrypto
```

---

### Background & Teaching

This section is the core of the challenge. Read it before writing a line of code.

#### Core Concepts

**The Merkle-Damgård Construction**

SHA-256 (like MD5, SHA-1) is built on the *Merkle-Damgård* construction.
The idea:

1. The input message is split into fixed-size **512-bit (64-byte) blocks**.
2. Before processing, the message is **padded** to a multiple of 64 bytes.
   The padding is always:
   - a single `0x80` byte
   - zero or more `0x00` bytes
   - an 8-byte big-endian encoding of the **original message length in bits**
3. The hash function maintains an **internal chaining state** — 8 × 32-bit
   words (256 bits total) — initialised to the SHA-256 IV constants.
4. Each block is processed by the compression function, which updates the
   chaining state.
5. **The final hash output IS the raw chaining state** after all blocks are
   processed.

This last fact is the root of the vulnerability.

**The Naive MAC and Why It Breaks**

Suppose a server computes `token = SHA256(key || msg)`.

When the hash function finishes, it has produced a 256-bit value that
encodes the internal state after processing `key || msg || <padding>`.
The padding is **deterministic**: given `len(key) + len(msg)`, you can
compute it exactly without knowing `key` or `msg`.

Now an attacker who observes `(msg, token)` knows:
- The chaining state after processing `key || msg || padding_1`. That
  state is exactly what `token` encodes — just re-interpret the 32-byte
  digest as eight big-endian uint32 words.
- The total number of bytes that were fed into SHA-256 before the state
  represented by `token` was reached: `KEY_LEN + len(msg) + len(padding_1)`.
  The attacker knows `KEY_LEN` (say it was published, or guessable) and
  `len(msg)` (observed on the wire).

The attacker can now **resume hashing** from the state encoded in `token`,
feeding any further data — say `"&admin=1"` — as the next block:

```
forged_token = SHA256_resume_from(token,
                   bytes_already_hashed = KEY_LEN + len(msg) + len(padding_1),
                   new_data = "&admin=1")
```

The forged token is a valid `SHA256(key || extended_msg)` where:
```
extended_msg = msg || padding_1 || "&admin=1"
```
The server, which recomputes `SHA256(key || extended_msg)`, will get the
exact same result as the attacker computed — yet the attacker never saw
the key.

**Why the padding matters for the extended message**

The server will compute padding for `key || extended_msg`.  The key length
is inside the padding itself (`len(key || extended_msg)` in bits at the
tail).  Because `padding_1` contains the length of `key || msg`, the
combined structure `key || msg || padding_1 || "&admin=1" || padding_2`
is *exactly* the SHA-256 input the attacker is simulating by continuing
from the leaked state.  The maths work out precisely because MD padding
is injective and deterministic.

**HMAC: The Correct Construction**

HMAC is defined as:
```
HMAC(key, msg) = H( (key ⊕ opad) || H( (key ⊕ ipad) || msg ) )
```
where `ipad = 0x36...` and `opad = 0x5c...` (both 64 bytes).

The inner hash hides the key/message interaction under a second hash that
uses a *different* key-derived value (`key ⊕ opad`).  Even if an attacker
could extend the inner hash, they would need to find a preimage of the
inner digest to produce a valid outer hash — computationally infeasible.
HMAC is provably secure as a PRF under standard assumptions on the
underlying hash, regardless of whether H uses Merkle-Damgård.

#### How to Approach This in C++

**Part A — Implementing the attack:**

1. **Re-interpret the valid token as SHA-256 state.** The 32-byte digest
   is eight big-endian 32-bit words. Read them with:
   ```cpp
   uint32_t state[8];
   for (int i = 0; i < 8; i++) {
       state[i] = (token[i*4]   << 24) | (token[i*4+1] << 16)
                | (token[i*4+2] <<  8) |  token[i*4+3];
   }
   ```

2. **Compute the padding for `key || original_msg`.**  Total length before
   padding: `KEY_LEN + original_msg.size()`.  Pad to next multiple of 64:
   - Append `0x80`.
   - Append `0x00` bytes until `(total_so_far % 64) == 56`.
   - Append the 8-byte big-endian bit-count of the original (unpadded)
     total: `(KEY_LEN + original_msg.size()) * 8`.

3. **Inject the recovered state into a fresh `SHA256_CTX`** using the
   low-level `SHA256_Init` / then manually override the ctx fields, OR
   use the OpenSSL trick: `SHA256_CTX` exposes its internal state publicly
   in `<openssl/sha.h>` (field `h[0..7]` and `Nl`/`Nh` for bit count):
   ```cpp
   SHA256_CTX ctx;
   SHA256_Init(&ctx);
   ctx.h[0] = state[0]; /* ... through h[7] */
   ctx.Nl = <total bits processed so far>;  // (KEY_LEN + padded_len) * 8
   ctx.Nh = 0;
   ctx.num = 0;  // no partial block buffered
   ```
   Then call `SHA256_Update(&ctx, extension.data(), extension.size())` and
   `SHA256_Final(forged.data(), &ctx)`.

4. **Construct `extended_msg`**: concatenate `original_msg`,
   the padding you computed in step 2, and `extension`.  This is the
   message the server will verify — prepend `key` mentally, but the server
   does it for you.

**Part B — Implementing HMAC-SHA256:**

Use the high-level `HMAC()` one-shot function:
```cpp
#include <openssl/hmac.h>
uint8_t out[EVP_MAX_MD_SIZE];
unsigned int out_len = 0;
HMAC(EVP_sha256(), SECRET_KEY, KEY_LEN,
     msg, msg_len,
     out, &out_len);
```
Copy `out_len` bytes into your return vector.  Do NOT use `SHA256_{Init,
Update, Final}` for HMAC — the wrapping with ipad/opad is what makes it
secure, and `HMAC()` does it correctly.

#### Key Pitfalls & Security/Correctness Gotchas

- **Big-endian state extraction**: SHA-256 stores its internal state words
  in big-endian byte order in the digest.  If you read them as native
  `uint32` on a little-endian machine without byte-swapping, your resumed
  hash will be silent garbage — no error, wrong answer.

- **The `Nl`/`Nh` bit counter in `SHA256_CTX`**: These are the low and high
  32 bits of the total number of bits processed *so far* (i.e., before the
  upcoming `SHA256_Update` call).  You must set `Nl` to
  `(padded_total_bytes * 8)` not `(original_bytes * 8)`.  Off-by-one on
  the padding length will produce a wrong forged token with no visible error.

- **`ctx.num` must be 0**: The `num` field in `SHA256_CTX` tracks how many
  bytes of a partial 64-byte block are buffered.  After you synthetic-resume
  at a block boundary you must set `ctx.num = 0`; otherwise OpenSSL will
  incorrectly flush a buffer of uninitialised bytes on the next `Update`.

- **`HMAC()` vs manual HMAC via SHA256**: Do not try to implement HMAC by
  hand using `SHA256_Init/Update/Final`; it is easy to make a mistake with
  the key padding (keys longer than 64 bytes must be hashed first).  Use
  the `HMAC()` function.

- **Constant-time comparison**: Production `server_verify_*` functions must
  use `CRYPTO_memcmp()` (OpenSSL) or equivalent, not `==` on `std::vector`,
  to prevent timing oracle attacks on the token comparison itself.  The
  scaffold uses `==` for simplicity; note this explicitly in your comments.

- **This attack requires knowing `KEY_LEN`**: The attacker must know (or
  brute-force) the key length to compute the correct padding.  Do not assume
  keeping the key length secret makes SHA256(key||msg) safe — it makes it
  slightly harder but not secure.

#### Bonus Curiosity (Optional — if you finish early)

- **SHA-3 (Keccak) is immune to length extension** by construction. It
  uses the *sponge* construction instead of Merkle-Damgård: the internal
  state is larger than the output, so the output does not uniquely
  determine the chaining state. Research why the rate/capacity split in
  Keccak prevents this attack entirely.

- **BLAKE3**: a modern hash function that is also length-extension immune,
  parallelisable, and faster than SHA-256 on modern CPUs. Its tree-hashing
  structure is interesting on its own merits.

- **Flickr's 2009 API vulnerability**: Flickr used `MD5(secret || params)`
  for API request signing and was susceptible to this exact attack. Reading
  the original disclosure gives a concrete taste of the real-world impact.

---

### Research Guidance

1. **`man HMAC`** — specifically the `HMAC()` one-shot function and
   `HMAC_CTX_{new,free}` for streaming use.
2. **`<openssl/sha.h>` source** — inspect the `SHA256_CTX` struct layout to
   find `h[8]`, `Nl`, `Nh`, and `num` directly.
3. **Kelsey & Schneier, "Second Preimages on n-bit Hash Functions for Much
   Less than 2^n Work"** (2004) — length extension is a by-product of
   the generic Merkle-Damgård second-preimage attack.
4. **RFC 2104** — the HMAC specification; §3 explains exactly why the
   nested construction defeats extension attacks.
5. **"Flickr's API Signature Forgery Vulnerability"** (Thai Duong & Juliano
   Rizzo, 2009) — real-world case study of this exact class of bug.

### Topics Covered
`SHA-256`, `Merkle-Damgård`, `length extension attack`, `HMAC`, `MAC forgery`, `OpenSSL EVP`, `cryptographic construction security`

### Completion Criteria
1. `./mac_challenge` compiles with `-Wall -Wextra -Wno-deprecated-declarations` and zero warnings.
2. `Part A` line prints `PASS — server accepted forged token`: the attacker
   successfully forged a MAC for the extended message without knowing the key.
3. The `extended_msg` produced by the attack contains the original message,
   correct SHA-256 padding, and the `"&admin=1"` extension — in that order.
4. `Part B` line prints `PASS — server rejected forged token`: the same
   forged token is rejected by the HMAC-based verifier.
5. `hmac_sha256_mac` uses `HMAC()` from `<openssl/hmac.h>`, not a manual
   ipad/opad construction.

### Difficulty Estimate
~20 min (knowing SHA-256 internals and OpenSSL's `SHA256_CTX` layout) | ~75 min (researching from scratch)

### Category
Security & Exploitation
