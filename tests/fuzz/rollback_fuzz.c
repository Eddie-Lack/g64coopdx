/* A libFuzzer target for the rollback core's message parsers (rb_host_receive and rb_client_receive).
 *
 * The first byte of each input picks the receiver and the sender; the rest is the message. One host and one client of
 * a 4-player toy game live across inputs, and both advance a frame now and then. Messages meet a moving state.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>

#include "../../src/rollback/rollback.h"
#include "../toy/toy.h"

static void drop(void *user, int to, const uint8_t *data, size_t len) {
    (void)user, (void)to, (void)data, (void)len;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static toy_game host_game, client_game;
    static rb_host *host;
    static rb_client *client;
    static unsigned calls;
    if (!host) {
        toy_init(&host_game, 4);
        toy_init(&client_game, 4);
        host = rb_host_new(&toy_ops, &host_game, 4, 3, drop, NULL, NULL, NULL);
        client = rb_client_new(&toy_ops, &client_game, 4, 1, 0, drop, NULL, NULL);
    }
    if (size < 1)
        return 0;
    if (data[0] & 0x80)
        rb_client_receive(client, data + 1, size - 1);
    else
        rb_host_receive(host, (int)(data[0] % 6) - 1, data + 1, size - 1);
    if (++calls % 16 == 0) {
        rb_input none = {0, 0, 0};
        rb_host_tick(host);
        rb_client_step(client, none);
    }
    return 0;
}
