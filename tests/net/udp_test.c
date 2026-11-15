/* Tests of the UDP socket (src/net/udp.c), and of the rollback core over real sockets on this machine: one host and
 * three clients of the toy game exchange their messages through UDP on 127.0.0.1.
 *
 * Usage: udp_test. Exits with 1 if a test fails. */
#include <stdio.h>
#include <string.h>

#include "../../src/net/udp.h"
#include "../../src/rollback/rollback.h"
#include "../toy/toy.h"

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static void test_resolve(void) {
    net_addr a;
    char err[128], text[64];
    check(net_resolve("127.0.0.1:5000", 1, &a, err, sizeof err) == 0 && a.family == 4 && a.port == 5000,
          "resolve 127.0.0.1:5000");
    net_addr_text(&a, text, sizeof text);
    check(!strcmp(text, "127.0.0.1:5000"), "print 127.0.0.1:5000");
    check(net_resolve("127.0.0.1", 7777, &a, err, sizeof err) == 0 && a.port == 7777, "a default port");
    check(net_resolve("[::1]:6000", 1, &a, err, sizeof err) == 0 && a.family == 6 && a.port == 6000,
          "resolve [::1]:6000");
    check(net_resolve("127.0.0.1:0", 1, &a, err, sizeof err) < 0 && net_resolve("127.0.0.1:x", 1, &a, err, sizeof err) < 0 &&
              net_resolve("127.0.0.1:70000", 1, &a, err, sizeof err) < 0 && net_resolve("", 1, &a, err, sizeof err) < 0 &&
              net_resolve("[::1", 1, &a, err, sizeof err) < 0,
          "bad addresses are refused");
}

static void test_exchange(void) {
    char err[128];
    net_socket *a = net_open(0, err, sizeof err), *b = net_open(0, err, sizeof err);
    net_addr to, from;
    uint8_t buf[NET_MAX_DATAGRAM], big[NET_MAX_DATAGRAM + 10];
    int n = -1, i;
    check(a && b, "two sockets open");
    if (!a || !b)
        return;
    net_resolve("127.0.0.1", net_port(b), &to, err, sizeof err);
    net_send(a, &to, "hello", 5);
    for (i = 0; i < 1000000 && n < 0; i++)
        n = net_recv(b, &from, buf, sizeof buf);
    check(n == 5 && !memcmp(buf, "hello", 5) && from.port == net_port(a), "a datagram goes from one socket to another");
    memset(big, 1, sizeof big);
    check(net_send(a, &to, big, sizeof big) < 0, "a datagram over the limit is not sent");
    check(net_recv(b, &from, buf, sizeof buf) < 0, "nothing more is waiting");
    net_close(a);
    net_close(b);
}

/* ---- the toy game over UDP */

#define PLAYERS 3
#define FRAMES 300

static net_socket *host_sock, *client_sock[PLAYERS];
static net_addr host_addr, client_addr[PLAYERS];
static uint64_t host_hash[FRAMES + 200], client_hash[PLAYERS][FRAMES + 200];
static uint32_t client_done[PLAYERS];

static void host_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)user;
    net_send(host_sock, &client_addr[to], data, len);
}

static void client_send(void *user, int to, const uint8_t *data, size_t len) {
    (void)to;
    net_send(client_sock[*(int *)user], &host_addr, data, len);
}

static void host_final(void *user, uint32_t frame, const void *state) {
    (void)user;
    if (frame < FRAMES + 200)
        host_hash[frame] = toy_ops.hash_state(state);
}

static void client_final(void *user, uint32_t frame, const void *state) {
    int id = *(int *)user;
    if (frame < FRAMES + 200)
        client_hash[id][frame] = toy_ops.hash_state(state);
    client_done[id] = frame + 1;
}

static rb_input controller(int p, uint32_t f) {
    rb_input in;
    in.x = (int8_t)(((int)(f / 7) * 37 + p * 11) % 161 - 80);
    in.y = (int8_t)(((int)(f / 5) * 53 + p * 17) % 161 - 80);
    in.buttons = (f / 9 + (uint32_t)p) % 4 == 0 ? 0x8000 : 0;
    return in;
}

static void test_toy_over_udp(void) {
    char err[128];
    static toy_game hg, cg[PLAYERS];
    static int ids[PLAYERS];
    rb_host *h;
    rb_client *c[PLAYERS];
    uint32_t tick, f, wrong = 0;
    int i, ok = 1;
    host_sock = net_open(0, err, sizeof err);
    net_resolve("127.0.0.1", net_port(host_sock), &host_addr, err, sizeof err);
    toy_init(&hg, PLAYERS);
    h = rb_host_new(&toy_ops, &hg, PLAYERS, 3, host_send, NULL, host_final, NULL);
    for (i = 0; i < PLAYERS; i++) {
        ids[i] = i;
        client_sock[i] = net_open(0, err, sizeof err);
        net_resolve("127.0.0.1", net_port(client_sock[i]), &client_addr[i], err, sizeof err);
        toy_init(&cg[i], PLAYERS);
        c[i] = rb_client_new(&toy_ops, &cg[i], PLAYERS, i, 0, client_send, client_final, &ids[i]);
    }
    for (tick = 0; tick < FRAMES + 100; tick++) {
        uint8_t buf[NET_MAX_DATAGRAM];
        net_addr from;
        int n, j;
        /* The host knows its clients by address, as the session layer will. */
        while ((n = net_recv(host_sock, &from, buf, sizeof buf)) >= 0)
            for (j = 0; j < PLAYERS; j++)
                if (net_addr_equal(&from, &client_addr[j]))
                    rb_host_receive(h, j, buf, (size_t)n);
        rb_host_tick(h);
        for (i = 0; i < PLAYERS; i++) {
            while ((n = net_recv(client_sock[i], &from, buf, sizeof buf)) >= 0)
                if (net_addr_equal(&from, &host_addr))
                    rb_client_receive(c[i], buf, (size_t)n);
            if (rb_client_frame(c[i]) < FRAMES)
                rb_client_step(c[i], controller(i, rb_client_frame(c[i])));
            else
                rb_client_idle(c[i]);
        }
    }
    for (i = 0; i < PLAYERS; i++) {
        if (client_done[i] < FRAMES)
            ok = 0;
        for (f = 0; f < FRAMES && f < client_done[i]; f++)
            if (client_hash[i][f] != host_hash[f])
                wrong++;
    }
    check(ok && wrong == 0 && rb_host_hash_mismatches(h) == 0 && rb_host_hash_matches(h) > 0,
          "the toy game over UDP: every client equals the host on every frame");
    for (i = 0; i < PLAYERS; i++) {
        rb_client_free(c[i]);
        net_close(client_sock[i]);
    }
    rb_host_free(h);
    net_close(host_sock);
}

int main(void) {
    if (net_init() != 0) {
        printf("FAIL the socket library did not start\n");
        return 1;
    }
    test_resolve();
    test_exchange();
    test_toy_over_udp();
    net_shutdown();
    printf("%s: %d failures\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
