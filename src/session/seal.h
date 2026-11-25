/* Sealed messages for the session (session.h): a key exchange at the hello, then every message encrypted and
 * signed with a key that only the two ends hold. The primitives are Monocypher's (third_party/monocypher): X25519
 * for the exchange, BLAKE2b for the keys, XChaCha20 with Poly1305 for the messages.
 *
 * What it gives: nobody on the path, the relay included, can read a sealed message, and nobody without the key
 * can forge one, change one or play one back. What it does not give: the players have no identities, so someone
 * who can change packets on the path during the hello (the relay can) could sit in the middle.
 * docs/discovery/netcode.md, "Encryption", has the whole account. */
#ifndef G64_SEAL_H
#define G64_SEAL_H

#include <stddef.h>
#include <stdint.h>

#define G64_SEAL_KEY 32
/* A sealed datagram's bytes around the message: the type, an 8-byte counter, and a 16-byte check. */
#define G64_SEAL_OVERHEAD 25
/* The first byte of a sealed datagram. */
#define G64_SEAL_TYPE 0x48

/* One end of a sealed link. */
typedef struct {
    uint8_t send[G64_SEAL_KEY], recv[G64_SEAL_KEY];
    /* The counter of the last message sent. A counter is never used twice with one key. */
    uint64_t sent;
    /* The highest counter received, and one bit for each of the 64 counters up to it: a message that comes twice,
       or one older than those 64, is dropped. Messages may still overtake each other. */
    uint64_t top, seen;
} g64_seal;

/* Random bytes from the operating system. 0, or -1 when it gave none: then no key is made. */
int g64_seal_random(uint8_t *out, size_t n);
/* For tests: another source of random bytes, or NULL for the system's. */
void g64_seal_set_random(int (*fn)(uint8_t *out, size_t n));

/* Clears a secret from memory, in a way the compiler does not remove. */
void g64_seal_wipe(void *secret, size_t n);

/* A new secret key and its public key. 0, or -1 without random bytes. */
int g64_seal_keypair(uint8_t secret[G64_SEAL_KEY], uint8_t pub[G64_SEAL_KEY]);

/* Makes the link's two keys from this end's secret key, the other end's public key, and the bytes of the hello and
 * the welcome that carried the public keys. Both ends get the same keys only if both saw the same two messages.
 * host is 1 at the host. 0, or -1 for a public key that gives no secret. */
int g64_seal_keys(g64_seal *s, int host, const uint8_t secret[G64_SEAL_KEY], const uint8_t theirs[G64_SEAL_KEY],
                  const uint8_t *hello, size_t helloLen, const uint8_t *welcome, size_t welcomeLen);

/* Seals a message of n bytes into out. It gives the datagram's length (n + G64_SEAL_OVERHEAD), or 0 when it does
 * not fit in max bytes. */
size_t g64_seal_lock(g64_seal *s, const uint8_t *plain, size_t n, uint8_t *out, size_t max);

/* Opens a sealed datagram into plain. It gives the message's length, or -1 for a datagram that is not sealed with
 * this link's key, was changed, came before, or does not fit in max bytes. */
int g64_seal_unlock(g64_seal *s, const uint8_t *d, size_t n, uint8_t *plain, size_t max);

#endif
