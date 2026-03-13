// Compile: gcc -Wall -Wextra -o main main.c
// Run: ./main www.example.com

// dns_query.c — scaffold
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
#define DNS_HEADER_FLAGS 0x0100

#define MAX_LABEL_TOTAL 255
#define MAX_LABEL_SEGMENT 63

/* encode_name: convert "www.example.com" → \x03www\x07example\x03com\x00
 * Writes into buf. Returns number of bytes written. */
int encode_name(const char *domain, uint8_t *buf) {
    uint8_t seg_i = 0;
    uint16_t buf_i = 0;
    uint8_t seg[MAX_LABEL_SEGMENT];
    for (uint16_t domain_i = 0; domain_i < strlen(domain) + 1; domain_i++) {
        if (domain[domain_i] == '.' || domain[domain_i] == '\0') {
            if (seg_i > MAX_LABEL_SEGMENT) return -1; // max label is 63
            buf[buf_i++] = seg_i;

            for(uint8_t i = 0; i < seg_i; i++)
                buf[buf_i++] = seg[i];

            memset(seg, 0, 63);
            seg_i = 0;
            continue;
        }
        seg[seg_i] = domain[domain_i];
        seg_i++;
    }
    buf[buf_i++] = 0;
    if (buf_i > MAX_LABEL_TOTAL) return -1; // max encoded name length is 255
    return (int)buf_i;
}

/* build_query: build the full UDP payload into buf, return total length */
int build_query(const char *domain, uint8_t *buf, uint16_t id) {

    int query_len = 0;

    // build header
    dns_header_t header;
    header.id = htons(id);
    header.flags = htons(DNS_HEADER_FLAGS);
    header.qdcount = htons(1);
    header.ancount = 0;
    header.nscount = 0;
    header.arcount = 0;
    memcpy(buf, (uint8_t *)&header, 12);
    query_len += 12;

    // build QNAME
    uint8_t encoded_domain[512];
    int domain_len = encode_name(domain, encoded_domain);
    if (domain_len < 0) return -1;
    memcpy(&buf[query_len], encoded_domain, domain_len);
    query_len += domain_len;

    // build QTYPE & QCLASS
    uint16_t type = htons(DNS_TYPE_A);
    memcpy(&buf[query_len], &type, sizeof(uint16_t));
    query_len += 2;
    uint16_t class = htons(DNS_CLASS_IN);
    memcpy(&buf[query_len], &class, sizeof(uint16_t));
    query_len += 2;

    return query_len;
}

void print_header(dns_header_t header) {
    uint16_t f = header.flags;
    uint8_t qr     = (f >> 15) & 0x1;
    uint8_t opcode = (f >> 11) & 0xF;
    uint8_t aa     = (f >> 10) & 0x1;
    uint8_t tc     = (f >>  9) & 0x1;
    uint8_t rd     = (f >>  8) & 0x1;
    uint8_t ra     = (f >>  7) & 0x1;
    uint8_t z      = (f >>  4) & 0x7;
    uint8_t rcode  = (f >>  0) & 0xF;

    printf("  ID:     %u\n", header.id);
    printf("  FLAGS:  0x%04X\n", f);
    printf("    QR:     %u (%s)\n",     qr,     qr ? "response" : "query");
    printf("    Opcode: %u (%s)\n",     opcode, opcode == 0 ? "standard query" : "other");
    printf("    AA:     %u\n",          aa);
    printf("    TC:     %u\n",          tc);
    printf("    RD:     %u\n",          rd);
    printf("    RA:     %u\n",          ra);
    printf("    Z:      %u\n",          z);
    printf("    RCODE:  %u (%s)\n",     rcode,  rcode == 0 ? "no error" : rcode == 3 ? "NXDOMAIN" : "other");
    printf("  QDCOUNT: %u\n", header.qdcount);
    printf("  ANCOUNT: %u\n", header.ancount);
    printf("  NSCOUNT: %u\n", header.nscount);
    printf("  ARCOUNT: %u\n", header.arcount);
}

// advances *pos past a name; returns 0 on success, -1 on error
int skip_name(const uint8_t *buf, int len, int *pos) {
    while (*pos < len) {
        uint8_t label_len = buf[*pos];
        if (label_len == 0) { (*pos)++; return 0; }          // root label
        if ((label_len & 0xC0) == 0xC0) { *pos += 2; return 0; } // pointer
        *pos += 1 + label_len;                                // normal label
    }
    return -1; // ran off the end
}

/* parse_response: parse buf of length len; print each A-record answer */
void parse_response(const uint8_t *buf, int len, uint16_t expected_id) {
    printf("Recv %d bytes\n\n", len);



    if (len < 12) {
        printf("Did not receive header\n");
        return ;
    }

    // grab header first
    dns_header_t header;
    header.id      = (uint16_t)buf[0]  << 8 | (uint16_t)buf[1];
    header.flags   = (uint16_t)buf[2]  << 8 | (uint16_t)buf[3];
    header.qdcount = (uint16_t)buf[4]  << 8 | (uint16_t)buf[5];
    header.ancount = (uint16_t)buf[6]  << 8 | (uint16_t)buf[7];
    header.nscount = (uint16_t)buf[8]  << 8 | (uint16_t)buf[9];
    header.arcount = (uint16_t)buf[10] << 8 | (uint16_t)buf[11];
    printf("Header recv:\n");
    print_header(header);

    if (expected_id != header.id) {
        printf("Header IDs do not match!\n");
        return;
    }

    printf("\n");

    int pos = 12; // skip header
    if (-1 == skip_name(buf, len, &pos)) {
        printf("Error skipping past name\n");
        return;
    }
    pos += 4; // skip QTYPE & QCLASS

    for(uint16_t an = 0; an < header.ancount; an++) {
        if (-1 == skip_name(buf, len, &pos)) {
            printf("Error skipping past name, in an = %u\n", an);
            return;
        }
        uint16_t type = (uint16_t)buf[pos] << 8 | (uint16_t)buf[pos+1];
        pos += 2;
        uint16_t class  = (uint16_t)buf[pos] << 8 | (uint16_t)buf[pos+1];
        pos += 2;
        uint32_t ttl  = (uint32_t)buf[pos] << 24 | (uint32_t)buf[pos+1] << 16 | (uint32_t)buf[pos+2] << 8 | (uint32_t)buf[pos+3];
        pos += 4;
        uint16_t rdlen = (uint16_t)buf[pos] << 8 | (uint16_t)buf[pos+1];
        pos += 2;

        if (type == DNS_TYPE_A && rdlen == 4) {
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &buf[pos], addr_str, sizeof(addr_str));
            printf("  A  %s  TTL=%u Class:%u\n", addr_str, ttl, class);
        }

        pos += rdlen;
    }
}    


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <domain>\n", argv[0]);
        return 1;
    }

    uint8_t pkt[512], resp[512];
    uint16_t id = (uint16_t)getpid();
    printf("Header ID: %u\n", id);
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
    parse_response(resp, rlen, id);
    return 0;
}