## Challenge [004] — DNS Query from Scratch

### Language
C

### Description
DNS is one of the most fundamental protocols in computing — practically every network operation begins with it — yet most engineers have only ever touched it through a high-level resolver (`getaddrinfo`, `gethostbyname`). In this challenge you will bypass the resolver entirely and speak DNS yourself, at the wire level.

You will write a C program that:
1. Constructs a valid DNS `A`-record query for a domain supplied on the command line
2. Sends it over UDP (port 53) to a public resolver (e.g., `8.8.8.8`)
3. Receives the binary response
4. Parses the response and prints every `A`-record answer — IPv4 address and TTL

No third-party DNS libraries. No `getaddrinfo`. You are allowed to use `sendto`/`recvfrom` and nothing higher.

The scaffold below gives you the header structure and function signatures. You must implement `encode_name`, `build_query`, and `parse_response`.

```c
// dns_query.c — scaffold
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* ---- DNS header (RFC 1035 §4.1.1) ---- */
typedef struct {
    uint16_t id;      /* query identifier                  */
    uint16_t flags;   /* QR | Opcode | AA | TC | RD | RA   */
    uint16_t qdcount; /* number of questions               */
    uint16_t ancount; /* number of answer RRs              */
    uint16_t nscount; /* number of authority RRs           */
    uint16_t arcount; /* number of additional RRs          */
} __attribute__((packed)) dns_header_t;

#define DNS_TYPE_A   1
#define DNS_CLASS_IN 1

/*
 * encode_name: convert "www.example.com" → \x03www\x07example\x03com\x00
 * Writes into buf. Returns number of bytes written.
 */
int encode_name(const char *domain, uint8_t *buf);

/* build_query: build the full UDP payload into buf, return total length */
int build_query(const char *domain, uint8_t *buf, uint16_t id);

/* parse_response: parse buf of length len; print each A-record answer */
void parse_response(const uint8_t *buf, int len);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <domain>\n", argv[0]);
        return 1;
    }

    uint8_t pkt[512], resp[512];
    uint16_t id = (uint16_t)getpid();
    int pkt_len = build_query(argv[1], pkt, id);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    /* Don't hang forever if the resolver doesn't answer */
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
    };
    inet_pton(AF_INET, "8.8.8.8", &srv.sin_addr);

    sendto(sock, pkt, pkt_len, 0, (struct sockaddr *)&srv, sizeof(srv));

    socklen_t slen = sizeof(srv);
    int rlen = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&srv, &slen);
    close(sock);

    if (rlen < 0) { perror("recvfrom"); return 1; }
    parse_response(resp, rlen);
    return 0;
}
```

---

### Background & Teaching

#### Core Concepts

**DNS Overview**

DNS (Domain Name System, RFC 1035) is a distributed hierarchical key-value store that maps names to resources. Every DNS query you send is a binary UDP datagram (fallback to TCP when the response exceeds 512 bytes) carrying a fixed-size 12-byte header followed by four variable-length sections.

**The DNS Message Format**

```
+--------------------+
|       Header       |  (12 bytes, always present)
+--------------------+
|      Question      |  (in practice always exactly 1 question)
+--------------------+
|       Answer       |  (zero or more resource records; present in responses)
+--------------------+
|     Authority      |  (NS records pointing to authoritative servers)
+--------------------+
|    Additional      |  (glue A records for the NS servers above)
+--------------------+
```

**The Header (12 bytes)**

| Field   | Bits | Meaning |
|---------|------|---------|
| ID      | 16   | Client-chosen random identifier; server echoes it back |
| QR      | 1    | 0 = query, 1 = response |
| Opcode  | 4    | 0 = standard query |
| AA      | 1    | Authoritative Answer (set by server) |
| TC      | 1    | Message was truncated |
| RD      | 1    | Recursion Desired — set by client to ask the server to recurse |
| RA      | 1    | Recursion Available (set by server) |
| Z       | 3    | Reserved, must be zero |
| RCODE   | 4    | Response code: 0 = no error, 3 = NXDOMAIN |
| QDCOUNT | 16   | Number of questions |
| ANCOUNT | 16   | Number of answers |
| NSCOUNT | 16   | Number of authority RRs |
| ARCOUNT | 16   | Number of additional RRs |

For a standard recursive query you send: `QR=0, Opcode=0, RD=1`, everything else zero. The flags word is therefore `0x0100` in host byte order (which becomes `0x01 0x00` on the wire — big-endian, so `htons(0x0100)`).

**The Question Section**

Each question contains:
- `QNAME`: the domain name in label-encoded form (see below)
- `QTYPE`: 2-byte type code (`1` = A record)
- `QCLASS`: 2-byte class code (`1` = IN, Internet)

**Label Encoding**

DNS does not send domain names as NUL-terminated strings. It uses a *label* encoding: each component is prefixed with a one-byte length, and the whole name is terminated with a zero-length label (representing the root):

```
"www.example.com"  →  \x03 w w w \x07 e x a m p l e \x03 c o m \x00
                       ^^^                                          ^^^
                       length=3                                  root label
```

Constraints: maximum label length is 63 bytes (the top 2 bits of the length byte are reserved for compression pointers). Maximum total encoded name length is 255 bytes including the root label.

**Resource Records (RR)**

Each answer in the response is a Resource Record with these fields in order:

| Field    | Size  | Meaning |
|----------|-------|---------|
| NAME     | var   | The owner name (may be a compression pointer) |
| TYPE     | 2 B   | Record type (1 = A) |
| CLASS    | 2 B   | Class (1 = IN) |
| TTL      | 4 B   | Time-to-live in seconds (unsigned) |
| RDLENGTH | 2 B   | Length of the RDATA field in bytes |
| RDATA    | var   | Record data; for A records: exactly 4 bytes of IPv4 address |

**DNS Message Compression**

To avoid repeating long domain names, DNS uses a pointer compression scheme. A name (or part of a name) may be replaced by a 2-byte pointer:

```
  Byte 0          Byte 1
  +--+--+--+--+--+--+--+--+   +--+--+--+--+--+--+--+--+
  | 1  1  OFFSET (high 6) |   |       OFFSET (low 8)   |
  +--+--+--+--+--+--+--+--+   +--+--+--+--+--+--+--+--+
```

If the top two bits of a length byte are both `1` (`byte & 0xC0 == 0xC0`), it is a pointer, not a length. The 14 remaining bits form an offset from the **start of the DNS message** where the name continues. You read from that offset until you hit a zero label or another pointer.

In practice, virtually every real DNS response compresses the answer NAME field. The common pattern `0xC0 0x0C` is a pointer to offset 12 — the first byte after the header — which is where the question's QNAME begins.

After following a pointer, your position in the *original* stream advances by exactly **2 bytes** (the pointer itself), not to the offset.

#### How to Approach This in C

**Building the query:**

1. Cast a `dns_header_t *` onto the start of your buffer. Fill `id = htons(id)`, `flags = htons(0x0100)`, `qdcount = htons(1)`. Leave the other counts zero.
2. Implement `encode_name`: split `domain` on `.` — use a manual scan (avoid `strtok` unless you `strdup` first, since `strtok` modifies its argument). For each segment, write `(uint8_t)strlen(segment)` then the segment bytes. Append `\x00`.
3. In `build_query`, write the header (12 bytes), then the encoded name, then `htons(DNS_TYPE_A)` and `htons(DNS_CLASS_IN)` as two `uint16_t` values. Return the total byte count written.

**Parsing the response:**

Use an integer index `pos` into the buffer, incrementing it as you consume bytes. This is safer than pointer arithmetic when handling pointers/jumps.

1. Cast the first 12 bytes as `dns_header_t *`. Verify `ntohs(hdr->flags) & 0x8000` (QR=1). Read `ancount = ntohs(hdr->ancount)`.
2. Skip the question section: write a `skip_name(buf, len, pos)` helper that advances `pos` past a label-encoded name (including following pointer chains). Then skip 4 more bytes (QTYPE + QCLASS).
3. For each answer RR (loop `ancount` times):
   - Skip the NAME field with `skip_name`.
   - Read TYPE (2 B), CLASS (2 B), TTL (4 B), RDLENGTH (2 B) — convert each with `ntohs`/`ntohl`.
   - If `type == DNS_TYPE_A` and `rdlength == 4`: call `inet_ntop(AF_INET, &buf[pos], addr_str, sizeof(addr_str))` and print with TTL.
   - Advance `pos` by `rdlength` regardless.

**The `skip_name` helper — handling compression:**

```c
// advances *pos past a name; returns 0 on success, -1 on error
int skip_name(const uint8_t *buf, int len, int *pos) {
    int hops = 0;
    while (*pos < len) {
        uint8_t label_len = buf[*pos];
        if (label_len == 0) { (*pos)++; return 0; }          // root label
        if ((label_len & 0xC0) == 0xC0) { *pos += 2; return 0; } // pointer
        *pos += 1 + label_len;                                // normal label
    }
    return -1; // ran off the end
}
```

Note: this correctly handles a name that starts immediately with a pointer (the common case in answer sections).

#### Key Pitfalls & Security/Correctness Gotchas

- **All multi-byte DNS fields are big-endian (network byte order).** Every `uint16_t` and `uint32_t` you read from the wire — including TYPE, CLASS, TTL, RDLENGTH — must pass through `ntohs`/`ntohl`. Forgetting this for TTL (`uint32_t`) or RDLENGTH is the single most common bug and produces garbage values silently.

- **Compression pointer loops can hang or crash your program.** A malformed or adversarial response could contain a pointer that points to itself or forms a cycle. Add a hop counter (max 10) in any name-following loop and return an error if exceeded.

- **Bounds-check every buffer access.** Before reading `buf[pos]` or `buf[pos..pos+N]`, verify `pos + N <= len`. A DNS response is at most 512 bytes for UDP (before EDNS0 extension). Trusting RDLENGTH from the wire without bounding it is a classic read-past-the-end vulnerability.

- **Always verify the response ID matches your query ID.** DNS over UDP is stateless — you could receive a stale or spoofed response from a different transaction. Check `ntohs(response_hdr->id) == id` before trusting the answer.

- **`strtok` modifies its input string in-place.** If you use it to split the domain for `encode_name`, either `strdup` the input first or use `strtok_r`. Surprising aliasing bugs occur otherwise if `domain` is a string literal or is used again after encoding.

- **Check RDLENGTH before using RDATA.** An A record has `RDLENGTH == 4`. Always assert this before calling `inet_ntop`; passing a wrong length silently reads unintended bytes as an IP address.

#### Bonus Curiosity (Optional — if you finish early)

- **DNS over HTTPS (DoH, RFC 8484)**: DNS queries encoded as the body of an HTTP/2 POST request with `Content-Type: application/dns-message`. The wire format is byte-for-byte identical to what you just wrote. Firefox uses this by default to route around your OS resolver — the same binary blob you crafted today travels inside an HTTPS stream.

- **DNS amplification DDoS**: Because a 60-byte `ANY` query can produce a 4000-byte response, and because UDP source addresses are spoofable, DNS has been used as an amplification vector at terabit scale. RFC 8482 now permits resolvers to return a minimal response to `ANY` queries. Run `dig ANY isc.org` and observe it.

- **DNSSEC**: Every RR set can be signed with an RRSIG record. The chain of trust runs from the root (`.`) downward via DS records. DNSSEC does not encrypt — it authenticates. Run `dig +dnssec google.com` to see RRSIG records appended to the answer section.

---

### Research Guidance

- **RFC 1035 §4** — the complete DNS message format; §4.1.4 covers the compression scheme with the bit-field layout diagram
- **RFC 1035 §3.2.2** — the full table of QTYPE values and what each means
- **`man 2 sendto` / `man 2 recvfrom`** — the UDP socket API on Linux; pay attention to the `sockaddr_in` layout and return value semantics
- **`man 3 inet_ntop`** — the correct, thread-safe way to format an IPv4 address; understand why `inet_ntoa` is deprecated
- **RFC 8482** — "Providing Minimal-Sized Responses to DNS Queries That Have QTYPE=ANY"; explains DNS amplification motivation

### Topics Covered
DNS wire format, RFC 1035, UDP sockets, network byte order, binary protocol parsing, label encoding, message compression pointers, C manual buffer management, `inet_ntop`, bounds checking

### Completion Criteria
1. `gcc -Wall -Wextra dns_query.c -o dns_query` produces zero warnings and zero errors.
2. Running `./dns_query google.com` prints at least one IPv4 address in dotted-decimal notation along with its TTL in seconds.
3. Running `./dns_query nonexistent.invalid` does not crash, does not hang indefinitely (the 5-second timeout fires), and exits cleanly — no segfault, no infinite loop.
4. The response ID is validated against the query ID before the response is parsed (verify by reading the code).
5. All multi-byte wire fields (TYPE, CLASS, TTL, RDLENGTH) are byte-swapped with `ntohs`/`ntohl` before use — confirm by reading the code or by verifying a correct TTL value is printed for `google.com` (expected: a small integer, not a garbage large number like `16777216`).

### Difficulty Estimate
~20 min (knowing the domain) | ~60 min (researching from scratch)

### Category
Networking & Protocols
