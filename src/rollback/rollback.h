/* The rollback core: the frame clock, input queues, prediction, savestates, confirmation by the host, and hash
 * checks (docs/discovery/netcode.md, "Recommendation: B with host-confirmed inputs").
 *
 * A client predicts every frame with the inputs it has and sends its own input to the host. The host forwards each
 * input at once to the other clients, which predict with it until the frame is confirmed. At a deadline the host
 * confirms each frame: it takes each player's input, or repeats that player's last input when it has not arrived. It
 * runs its own copy of the game on the confirmed inputs alone, and sends the confirmed inputs to every client. A
 * client that predicted wrong loads the savestate of the first wrong frame and runs the frames again. Clients send the
 * hash of a confirmed frame now and then, and the host compares it with its own copy's hash.
 *
 * The core knows the game only through rb_game_ops, and the network only through a send function. It reads no clock:
 * the caller calls the tick functions once per game frame. Every message it receives is checked before use. */
#ifndef G64_ROLLBACK_H
#define G64_ROLLBACK_H

#include <stddef.h>
#include <stdint.h>

#define RB_MAX_PLAYERS 16
/* The most frames of savestates and inputs that a client keeps; rb_client_new takes a smaller window for a game
 * with large savestates. A client does not predict further ahead than its window. */
#define RB_WINDOW 128
/* Frames of confirmed inputs and reference hashes that the host keeps. */
#define RB_HOST_WINDOW 256
/* Most own inputs that one input message carries: the newest, and earlier ones again against loss. */
#define RB_INPUT_REPEAT 8
/* Most confirmed frames in one message. */
#define RB_CONFIRM_BATCH 32
/* A client sends the hash of every confirmed frame whose number is a multiple of this. */
#define RB_HASH_EVERY 10

/* One player's controller in one frame, and whether the player is in the game in that frame. A client sends only
 * its controller; the host decides who is present, and the confirmed inputs carry it. A game sees a player arrive
 * when present turns 1 and leave when it turns 0. */
typedef struct {
    uint16_t buttons;
    int8_t x, y;
    uint8_t present;
} rb_input;

/* The game, as the core sees it. A savestate is state_size bytes. hash_state must give the same value for the
 * same game state on every machine. */
typedef struct {
    size_t state_size;
    void (*step)(void *game, const rb_input *inputs, int players);
    void (*save)(void *game, void *state);
    void (*load)(void *game, const void *state);
    uint64_t (*hash_state)(const void *state);
} rb_game_ops;

/* Sends one message. A client sends to the host (to is 0); the host sends to client number to. */
typedef void (*rb_send_fn)(void *user, int to, const uint8_t *data, size_t len);
/* Called once for each frame whose state is final: confirmed, and run on confirmed inputs. state is the savestate
 * after that frame; it is valid only during the call. The core hashes only the frames whose hashes it sends. */
typedef void (*rb_frame_fn)(void *user, uint32_t frame, const void *state);
/* Called by the host for each frame it confirms, with the confirmed inputs. */
typedef void (*rb_confirm_fn)(void *user, uint32_t frame, const rb_input *inputs, int players);

/* The largest message the core sends: a confirm message is cut to RB_MAX_WIRE bytes, which leaves room for a
 * transport's header in one UDP datagram. RB_MAX_MESSAGE is the largest that a receiver accepts. */
#define RB_MAX_WIRE 1100
#define RB_MAX_MESSAGE (10 + RB_CONFIRM_BATCH * (2 + RB_MAX_PLAYERS * 4))

typedef struct rb_client rb_client;
typedef struct rb_host rb_host;

/* A client for player me of players. game is in its start state. window is the number of savestates it keeps
 * (8 to RB_WINDOW; 0 means RB_WINDOW). Returns NULL on bad arguments or no memory. */
rb_client *rb_client_new(const rb_game_ops *ops, void *game, int players, int me, int window, rb_send_fn send,
                         rb_frame_fn on_final, void *user);
/* The same, for a client that starts at frame start: game is in the state after frame start - 1, and last holds
 * that frame's confirmed inputs (the prediction for the next frames; NULL: all zero and present). Frames before start
 * are final. A player who joins a running game starts this way, after running its confirmed frames. */
rb_client *rb_client_new_at(const rb_game_ops *ops, void *game, int players, int me, int window, uint32_t start,
                            const rb_input *last, rb_send_fn send, rb_frame_fn on_final, void *user);
void rb_client_free(rb_client *c);
/* Runs the next frame with the local player's input, if the client is not too far ahead of the confirmed frames.
 * Returns 1 if it ran a frame, 0 if it must wait. Sends the input to the host either way. */
int rb_client_step(rb_client *c, rb_input local);
/* Sends the host the client's latest inputs and the frames it has confirmed, without running a frame. Call it on a
 * frame in which the client runs no frame. */
void rb_client_idle(rb_client *c);
/* Handles one message from the host. Returns 0, or -1 if the message is malformed (it is then ignored). */
int rb_client_receive(rb_client *c, const uint8_t *data, size_t len);
/* Frames run so far, and frames confirmed so far. */
uint32_t rb_client_frame(const rb_client *c);
uint32_t rb_client_confirmed(const rb_client *c);
/* Desync reports received from the host, and frames run again after a wrong prediction. */
uint32_t rb_client_desyncs(const rb_client *c);
uint64_t rb_client_rollback_frames(const rb_client *c);
/* Other players' inputs received as hints (the host forwards each input before it confirms it). */
uint32_t rb_client_hints(const rb_client *c);

/* The host for players clients. game is in its start state; it becomes the reference copy. deadline is how many
 * frames after its own frame count the host confirms a frame (at least 1). Every player starts present. */
rb_host *rb_host_new(const rb_game_ops *ops, void *game, int players, uint32_t deadline, rb_send_fn send,
                     rb_confirm_fn on_confirm, rb_frame_fn on_final, void *user);
void rb_host_free(rb_host *h);
/* Advances the host's frame clock by one frame, confirms the frames whose deadline has passed, runs them on the
 * reference copy, and sends each client the confirmed frames it has not acknowledged. */
void rb_host_tick(rb_host *h);
/* Handles one message from client from. Returns 0, or -1 if the message is malformed (it is then ignored). */
int rb_host_receive(rb_host *h, int from, const uint8_t *data, size_t len);
/* Player p left: from the next confirmed frame on, their input is 0 and their messages are refused. */
void rb_host_drop_player(rb_host *h, int p);
/* Player p is present (1) or not (0) from the next frame the host confirms, rb_host_confirmed(h). The host does not
 * wait for the input of a player who is not present: it confirms 0, and ignores their inputs. When p becomes present,
 * the host sends them confirmed frames from that frame on; their client starts there (rb_client_new_at). */
void rb_host_set_present(rb_host *h, int p, int present);
int rb_host_present(const rb_host *h, int p);
uint32_t rb_host_confirmed(const rb_host *h);
/* Hashes received from clients that matched, and that did not. */
uint32_t rb_host_hash_matches(const rb_host *h);
uint32_t rb_host_hash_mismatches(const rb_host *h);
/* Inputs that arrived after their frame was confirmed. */
uint32_t rb_host_late_inputs(const rb_host *h);

#endif
