/* Tests of the session's sealed messages (src/session/seal.h): known answers for the primitives, then the link:
 * both ends get the same keys, a changed, repeated, too old or foreign datagram doesn't open, and datagrams may
 * overtake each other. It prints a hash of what a fixed "random" source gives; Linux and Windows must print the
 * same. No ROM needed. */
#include <stdio.h>
#include <string.h>

#include "../../src/session/seal.h"
#include "../../third_party/monocypher/monocypher.h"

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void hex(uint8_t *out, const char *s) {
    size_t i;
    for (i = 0; s[2 * i]; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* A fixed source of "random" bytes for the tests. */
static uint8_t fixedNext;
static int fixed_random(uint8_t *out, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)(fixedNext++ * 37 + 11);
    return 0;
}
static int no_random(uint8_t *out, size_t n) {
    (void)out, (void)n;
    return -1;
}

int main(void) {
    uint8_t a[32], b[32], pa[32], pb[32], want[64], got[64];
    /* X25519: the example of RFC 7748, section 6.1. */
    hex(a, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    hex(b, "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    crypto_x25519_public_key(pa, a);
    crypto_x25519_public_key(pb, b);
    hex(want, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    check(memcmp(pa, want, 32) == 0, "X25519: the first public key of RFC 7748");
    hex(want, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    check(memcmp(pb, want, 32) == 0, "X25519: the second public key of RFC 7748");
    hex(want, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    crypto_x25519(got, a, pb);
    check(memcmp(got, want, 32) == 0, "X25519: the shared secret of RFC 7748");
    crypto_x25519(got, b, pa);
    check(memcmp(got, want, 32) == 0, "X25519: the shared secret from the other side");
    /* BLAKE2b: the example of RFC 7693, appendix A. */
    hex(want, "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
              "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
    crypto_blake2b(got, 64, (const uint8_t *)"abc", 3);
    check(memcmp(got, want, 64) == 0, "BLAKE2b: the example of RFC 7693");

    {
        /* The link. */
        static const uint8_t hello[] = "a hello", welcome[] = "a welcome";
        uint8_t hs[32], hp[32], js[32], jp[32], d[256], d2[256], plain[256], zero[32] = {0};
        g64_seal host, join, other;
        size_t n, n2;
        int i;
        g64_seal_set_random(no_random);
        check(g64_seal_keypair(hs, hp) != 0, "no key without random bytes");
        g64_seal_set_random(fixed_random);
        check(g64_seal_keypair(hs, hp) == 0 && g64_seal_keypair(js, jp) == 0, "key pairs");
        check(g64_seal_keys(&host, 1, hs, jp, hello, sizeof hello, welcome, sizeof welcome) == 0, "the host's keys");
        check(g64_seal_keys(&join, 0, js, hp, hello, sizeof hello, welcome, sizeof welcome) == 0, "the joiner's keys");
        check(memcmp(host.send, join.recv, 32) == 0 && memcmp(host.recv, join.send, 32) == 0, "both ends agree");
        check(memcmp(host.send, host.recv, 32) != 0, "each direction has its own key");
        check(g64_seal_keys(&other, 0, js, zero, hello, sizeof hello, welcome, sizeof welcome) != 0,
              "a public key that gives no secret is refused");
        /* Another hello gives other keys. */
        check(g64_seal_keys(&other, 0, js, hp, (const uint8_t *)"b hello", 8, welcome, sizeof welcome) == 0 &&
                  memcmp(other.send, join.send, 32) != 0,
              "a changed hello gives other keys");

        n = g64_seal_lock(&join, (const uint8_t *)"hi there", 8, d, sizeof d);
        check(n == 8 + G64_SEAL_OVERHEAD && d[0] == G64_SEAL_TYPE, "a sealed datagram's size and type");
        for (i = 0; i + 8 <= (int)n && memcmp(d + i, "hi there", 8) != 0; i++) {
        }
        check(i + 8 > (int)n, "the text is not readable in the datagram");
        check(g64_seal_unlock(&other, d, n, plain, sizeof plain) < 0, "another key does not open it");
        for (i = 0; i < (int)n; i++) {
            memcpy(d2, d, n);
            d2[i] ^= 0x20;
            if (g64_seal_unlock(&host, d2, n, plain, sizeof plain) >= 0)
                break;
        }
        check(i == (int)n, "a datagram with any byte changed does not open");
        check(g64_seal_unlock(&host, d, n - 1, plain, sizeof plain) < 0, "a cut datagram does not open");
        check(g64_seal_unlock(&host, d, n, plain, 7) < 0, "a datagram too long for the buffer does not open");
        check(g64_seal_unlock(&host, d, n, plain, sizeof plain) == 8 && memcmp(plain, "hi there", 8) == 0, "it opens");
        check(g64_seal_unlock(&host, d, n, plain, sizeof plain) < 0, "it does not open twice");
        check(g64_seal_unlock(&join, d, n, plain, sizeof plain) < 0, "it does not open at its sender");
        check(g64_seal_lock(&join, plain, 250, d, sizeof d) == 0, "a message too long for the datagram is not sealed");

        /* Datagrams may overtake each other, each still opens once, and one older than the window doesn't. */
        n = g64_seal_lock(&join, (const uint8_t *)"two", 3, d, sizeof d);
        n2 = g64_seal_lock(&join, (const uint8_t *)"three", 5, d2, sizeof d2);
        check(g64_seal_unlock(&host, d2, n2, plain, sizeof plain) == 5, "the later datagram first");
        check(g64_seal_unlock(&host, d, n, plain, sizeof plain) == 3, "then the earlier one");
        check(g64_seal_unlock(&host, d, n, plain, sizeof plain) < 0 && g64_seal_unlock(&host, d2, n2, plain, sizeof plain) < 0,
              "neither opens again");
        n = g64_seal_lock(&join, (const uint8_t *)"old", 3, d, sizeof d);
        for (i = 0; i < 70; i++) {
            n2 = g64_seal_lock(&join, (const uint8_t *)"new", 3, d2, sizeof d2);
            check(g64_seal_unlock(&host, d2, n2, plain, sizeof plain) == 3, "a run of datagrams");
        }
        check(g64_seal_unlock(&host, d, n, plain, sizeof plain) < 0, "a datagram older than the window does not open");
        /* An empty message. */
        n = g64_seal_lock(&host, plain, 0, d, sizeof d);
        check(n == G64_SEAL_OVERHEAD && g64_seal_unlock(&join, d, n, plain, sizeof plain) == 0, "an empty message");

        /* The same on every platform. */
        crypto_blake2b(got, 8, d2, n2);
        printf("seal: fixed run %02x%02x%02x%02x%02x%02x%02x%02x\n", got[0], got[1], got[2], got[3], got[4], got[5],
               got[6], got[7]);
        g64_seal_set_random(NULL);
        check(g64_seal_random(a, 32) == 0 && g64_seal_random(b, 32) == 0 && memcmp(a, b, 32) != 0,
              "the system's random bytes");
    }
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
