# The lobby server

The lobby server introduces players to each other. Its finished form lists open sessions, passes connection details
between a host and a joiner, and hands out short-lived credentials for the relay. It never sees game data.

It answers the routes below and runs the relay (`relay.go`): a UDP service on port 27065 that carries the game's
packets between a host and its joiners inside a room, so no player forwards a port. A host opens a room and gets a
six-character code; joiners join with the code. The relay never reads the game's packets. A host can put its room on a list, with a title. The game asks the relay
for the list over UDP (the rooms of its own build, 12 at most); a room leaves the list 10 seconds after its host's
last announcement. The request is as long as the longest answer, so the relay never sends more than it received. A
host can also ban a joiner: the relay then refuses that joiner's address for the room. The other HTTP routes are
still a placeholder. Phase 4 of the
[roadmap](../docs/discovery/roadmap.md) writes the real one. [docs/hosting.md](../docs/hosting.md) says where it
runs.

| Route | Answer |
| --- | --- |
| `GET /healthz` | `ok` |
| `GET /v1/info` | The server's name, version and protocol version, `"placeholder": true`, and the relay's port with its open rooms and players |
| `GET /v1/sessions` | The rooms on the list: code, title, wanted and joined players |
| `GET /v1/ice` | The STUN servers a client should use |

## Settings

The server reads its settings from environment variables.

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `8080` | The TCP port to listen on |
| `LOBBY_STUN_URLS` | `stun:stun.cloudflare.com:3478` | STUN servers, separated by commas |
| `LOBBY_RELAY_PORT` | `27065` | The relay's UDP port; 0 turns the relay off. On Fly.io (`FLY_APP_NAME` set) it binds to `fly-global-services` |

## Run it

```sh
make server && build/lobby        # from the repository's root; then open http://localhost:8080/v1/info
make test                         # format check, vet and tests
make image                        # the container image (needs Docker)
```

The server is written in Go with the standard library only. It is one static program in an empty container image.
