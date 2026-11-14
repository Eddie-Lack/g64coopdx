# The lobby server

The lobby server introduces players to each other. Its finished form lists open sessions, passes connection details
between a host and a joiner, and hands out short-lived credentials for the relay. It never sees game data.

**This is a placeholder.** It answers the routes below and does nothing else. Phase 4 of the
[roadmap](../docs/discovery/roadmap.md) writes the real one. [docs/hosting.md](../docs/hosting.md) says where it
runs.

| Route | Answer |
| --- | --- |
| `GET /healthz` | `ok` |
| `GET /v1/info` | The server's name, version and protocol version, and `"placeholder": true` |
| `GET /v1/sessions` | An empty list |
| `GET /v1/ice` | The STUN servers a client should use |

## Settings

The server reads its settings from environment variables.

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `8080` | The TCP port to listen on |
| `LOBBY_STUN_URLS` | `stun:stun.cloudflare.com:3478` | STUN servers, separated by commas |

## Run it

```sh
make server && build/lobby        # from the repository's root; then open http://localhost:8080/v1/info
make test                         # format check, vet and tests
make image                        # the container image (needs Docker)
```

The server is written in Go with the standard library only. It is one static program in an empty container image.
