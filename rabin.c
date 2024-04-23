// #include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "rabin.h"

#define MASK ((1<<AVERAGE_BITS)-1)
#define POL_SHIFT (POLYNOMIAL_DEGREE-8)

struct chunk_t last_chunk;

static bool tables_initialized = false;
static uint64_t mod_table[256];
static uint64_t out_table[256];

static int deg(uint64_t p) {
    uint64_t mask = 0x8000000000000000LL;

    for (int i = 0; i < 64; i++) {
        if ((mask & p) > 0) {
            return 63 - i;
        }

        mask >>= 1;
    }

    return -1;
}

// Mod calculates the remainder of x divided by p.
static uint64_t mod(uint64_t x, uint64_t p) {
    // printf("%lld mod %lld = %lld,", x, p, x % p);
    int cnt = 0;
    uint64_t xx = x;
    uint64_t xxx;
    while (deg(x) >= deg(p)) {
        unsigned int shift = deg(x) - deg(p);

        x = x ^ (p << shift);
        if(cnt == 0)
        {
            xxx = x;
        }
        cnt++;
    }

    // printf("%lld\n", x);
    if(cnt > 1)
    {
        // printf("mod cnt larger than 1: %llx, %llx, %llx, %d, %d, %d \n", xx, xxx, p, deg(xx), deg(xxx), deg(p));
    }
    return x;
}

static uint64_t append_byte(uint64_t hash, uint8_t b, uint64_t pol) {
    hash <<= 8;
    hash |= (uint64_t)b;

    return mod(hash, pol);
    // return hash;
}

static void calc_tables(void) {
    // calculate table for sliding out bytes. The byte to slide out is used as
    // the index for the table, the value contains the following:
    // out_table[b] = Hash(b || 0 ||        ...        || 0)
    //                          \ windowsize-1 zero bytes /
    // To slide out byte b_0 for window size w with known hash
    // H := H(b_0 || ... || b_w), it is sufficient to add out_table[b_0]:
    //    H(b_0 || ... || b_w) + H(b_0 || 0 || ... || 0)
    //  = H(b_0 + b_0 || b_1 + 0 || ... || b_w + 0)
    //  = H(    0     || b_1 || ...     || b_w)
    //
    // Afterwards a new byte can be shifted in.
    for (int b = 0; b < 256; b++) {
        uint64_t hash = 0;

        hash = append_byte(hash, (uint8_t)b, POLYNOMIAL);
        for (int i = 0; i < WINSIZE-1; i++) {
            hash = append_byte(hash, 0, POLYNOMIAL);
        }
        out_table[b] = hash;
        printf("out.%d: %llx\n", b, hash);
    }

    // calculate table for reduction mod Polynomial
    int k = deg(POLYNOMIAL);
    for (int b = 0; b < 256; b++) {
        // mod_table[b] = A | B, where A = (b(x) * x^k mod pol) and  B = b(x) * x^k
        //
        // The 8 bits above deg(Polynomial) determine what happens next and so
        // these bits are used as a lookup to this table. The value is split in
        // two parts: Part A contains the result of the modulus operation, part
        // B is used to cancel out the 8 top bits so that one XOR operation is
        // enough to reduce modulo Polynomial
        mod_table[b] = mod(((uint64_t)b) << k, POLYNOMIAL) | ((uint64_t)b) << k;
        printf("mod.%d: %llx\n", b, mod_table[b]);
        printf("mod.%d: %llx, %llx\n", b, mod(((uint64_t)b) << k, POLYNOMIAL), ((uint64_t)b) << k);
    }
}

uint64_t mod_test(uint64_t x, uint64_t p) {
    // return mod(x, p);
    calc_tables();
    return 0;
}

void rabin_append(struct rabin_t *h, uint8_t b) {
    uint8_t index = (uint8_t)(h->digest >> POL_SHIFT);
    h->digest <<= 8;
    h->digest |= (uint64_t)b;
    h->digest ^= mod_table[index];
}

void rabin_slide(struct rabin_t *h, uint8_t b) {
    uint8_t out = h->window[h->wpos];
    h->window[h->wpos] = b;
    h->digest = (h->digest ^ out_table[out]);
    h->wpos = (h->wpos +1 ) % WINSIZE;
    rabin_append(h, b);
}

void rabin_reset(struct rabin_t *h) {
    for (int i = 0; i < WINSIZE; i++)
        h->window[i] = 0;
    h->digest = 0;
    h->wpos = 0;
    h->count = 0;
    h->digest = 0;

    rabin_slide(h, 1);
}

int rabin_next_chunk(struct rabin_t *h, uint8_t *buf, unsigned int len) {
    for (unsigned int i = 0; i < len; i++) {
        uint8_t b = *buf++;

        rabin_slide(h, b);

        h->count++;
        h->pos++;

        if ((h->count >= MINSIZE && ((h->digest & MASK) == 0)) || h->count >= MAXSIZE) {
            last_chunk.start = h->start;
            last_chunk.length = h->count;
            last_chunk.cut_fingerprint = h->digest;

            // keep position
            unsigned int pos = h->pos;
            rabin_reset(h);
            h->start = pos;
            h->pos = pos;

            return i+1;
        }
    }

    return -1;
}

struct rabin_t *rabin_init(void) {
    if (!tables_initialized) {
        calc_tables();
        tables_initialized = true;
    }

    struct rabin_t *h;

    if ((h = malloc(sizeof(struct rabin_t))) == NULL) {
        // errx(1, "malloc()");
        fprintf(stderr, "malloc()");
        exit(1);
    }

    rabin_reset(h);

    return h;
}


struct chunk_t *rabin_finalize(struct rabin_t *h) {
    if (h->count == 0) {
        last_chunk.start = 0;
        last_chunk.length = 0;
        last_chunk.cut_fingerprint = 0;
        return NULL;
    }

    last_chunk.start = h->start;
    last_chunk.length = h->count;
    last_chunk.cut_fingerprint = h->digest;
    return &last_chunk;
}
