/* Sealed messages for the session (seal.h). */
#include "seal.h"

#include <string.h>

#include "../../third_party/monocypher/monocypher.h"

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static int (*randomFn)(uint8_t *out, size_t n);

void g64_seal_set_random(int (*fn)(uint8_t *out, size_t n)) { randomFn = fn; }

int g64_seal_random(uint8_t *out, size_t n) {
    if (randomFn)
        return randomFn(out, n);
#ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    {
        /* The kernel's generator. A short read is read on; any failure gives no key. */
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        size_t got = 0;
        if (fd < 0)
            return -1;
        while (got < n) {
            ssize_t r = read(fd, out + got, n - got);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                break;
            got += (size_t)r;
        }
        close(fd);
        return got == n ? 0 : -1;
    }
#endif
}

int g64_seal_keypair(uint8_t secret[G64_SEAL_KEY], uint8_t pub[G64_SEAL_KEY]) {
    if (g64_seal_random(secret, G64_SEAL_KEY) != 0) {
        crypto_wipe(secret, G64_SEAL_KEY);
        return -1;
    }
    crypto_x25519_public_key(pub, secret);
    return 0;
}

int g64_seal_keys(g64_seal *s, int host, const uint8_t secret[G64_SEAL_KEY], const uint8_t theirs[G64_SEAL_KEY],
                  const uint8_t *hello, size_t helloLen, const uint8_t *welcome, size_t welcomeLen) {
    static const uint8_t zero[G64_SEAL_KEY];
    static const char label[] = "g64coopdx session 1";
    uint8_t shared[G64_SEAL_KEY], keys[2 * G64_SEAL_KEY], lens[8];
    crypto_blake2b_ctx ctx;
    crypto_x25519(shared, secret, theirs);
    /* A few public keys give an all-zero secret whatever this end's key is. A link must not be built on one. */
    if (crypto_verify32(shared, zero) == 0) {
        crypto_wipe(shared, sizeof shared);
        return -1;
    }
    /* The two keys: a keyed hash of the label and of both messages, each with its length in front. */
    lens[0] = (uint8_t)helloLen, lens[1] = (uint8_t)(helloLen >> 8), lens[2] = (uint8_t)(helloLen >> 16);
    lens[3] = (uint8_t)(helloLen >> 24);
    lens[4] = (uint8_t)welcomeLen, lens[5] = (uint8_t)(welcomeLen >> 8), lens[6] = (uint8_t)(welcomeLen >> 16);
    lens[7] = (uint8_t)(welcomeLen >> 24);
    crypto_blake2b_keyed_init(&ctx, sizeof keys, shared, sizeof shared);
    crypto_blake2b_update(&ctx, (const uint8_t *)label, sizeof label);
    crypto_blake2b_update(&ctx, lens, sizeof lens);
    crypto_blake2b_update(&ctx, hello, helloLen);
    crypto_blake2b_update(&ctx, welcome, welcomeLen);
    crypto_blake2b_final(&ctx, keys);
    /* The first key seals what the joiner sends, the second what the host sends. */
    {
        g64_seal fresh;
        memset(&fresh, 0, sizeof fresh);
        memcpy(host ? fresh.recv : fresh.send, keys, G64_SEAL_KEY);
        memcpy(host ? fresh.send : fresh.recv, keys + G64_SEAL_KEY, G64_SEAL_KEY);
        *s = fresh;
        crypto_wipe(&fresh, sizeof fresh);
    }
    crypto_wipe(shared, sizeof shared);
    crypto_wipe(keys, sizeof keys);
    return 0;
}

/* The 24-byte number that goes with a message: the counter, then zeros. Each direction has its own key, so the two
   ends' counters never meet. */
static void nonce_of(uint8_t nonce[24], const uint8_t *counter) {
    memset(nonce, 0, 24);
    memcpy(nonce, counter, 8);
}

size_t g64_seal_lock(g64_seal *s, const uint8_t *plain, size_t n, uint8_t *out, size_t max) {
    uint8_t nonce[24];
    uint64_t c;
    int i;
    if (n + G64_SEAL_OVERHEAD > max || s->sent == UINT64_MAX)
        return 0;
    c = ++s->sent;
    out[0] = G64_SEAL_TYPE;
    for (i = 0; i < 8; i++)
        out[1 + i] = (uint8_t)(c >> (8 * i));
    nonce_of(nonce, out + 1);
    /* The type and the counter are signed with the message. */
    crypto_aead_lock(out + 9, out + 9 + n, s->send, nonce, out, 9, plain, n);
    return n + G64_SEAL_OVERHEAD;
}

int g64_seal_unlock(g64_seal *s, const uint8_t *d, size_t n, uint8_t *plain, size_t max) {
    uint8_t nonce[24];
    uint64_t c = 0;
    size_t len;
    int i;
    if (n < G64_SEAL_OVERHEAD || d[0] != G64_SEAL_TYPE)
        return -1;
    len = n - G64_SEAL_OVERHEAD;
    if (len > max)
        return -1;
    for (i = 0; i < 8; i++)
        c |= (uint64_t)d[1 + i] << (8 * i);
    /* A counter that came before, or one too old to remember. */
    if (c == 0 || (c <= s->top && (s->top - c >= 64 || (s->seen >> (s->top - c)) & 1)))
        return -1;
    nonce_of(nonce, d + 1);
    if (crypto_aead_unlock(plain, d + 9 + len, s->recv, nonce, d, 9, d + 9, len) != 0)
        return -1;
    /* Only a message that passed the check moves the window. */
    if (c > s->top) {
        s->seen = c - s->top >= 64 ? 0 : s->seen << (c - s->top);
        s->seen |= 1;
        s->top = c;
    } else {
        s->seen |= (uint64_t)1 << (s->top - c);
    }
    return (int)len;
}
