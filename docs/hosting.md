# Hosting

This file says where g64coopdx's online services and downloads run, why, what is set up today, and what the owner
still has to do. Prices and limits were read from each provider's own pages on 14 November 2026. Check them again
before paying for anything.

"Hosting" here means the project's own services. Who hosts a game session is a different question: one player's
machine does ([discovery/netcode.md](discovery/netcode.md#recommendation-b-with-host-confirmed-inputs)).

## What needs hosting

| Thing | What it does | Needed from |
| --- | --- | --- |
| The lobby server | Lists open sessions; passes connection details between a host and a joiner; hands out relay credentials. It speaks HTTPS and never sees game data | Phase 4 |
| STUN | Tells a machine its public address | Phase 4 |
| The relay (TURN) | Carries the game's packets when two machines cannot reach each other directly | Phase 4 |
| Downloads | The released Windows and Linux builds | Phase 6 |
| The source and CI | The repository and its tests | Now |

The first playable versions (phases 1 to 3) connect directly and need no service
([discovery/roadmap.md](discovery/roadmap.md)).

## The decision

| Thing | Where it runs | Cost while idle |
| --- | --- | --- |
| The lobby server | A small container on Fly.io that stops when nobody is connected | A few cents a month |
| STUN | Cloudflare's public STUN server | Free |
| The relay | Cloudflare Realtime TURN, a managed service | Free |
| Downloads | GitHub Releases | Free |
| The lobby's container image | The GitHub container registry (ghcr.io) | Free |
| The source and CI | GitHub and GitHub Actions | Free |

No domain name is bought. The project's name is still open
([discovery/open-questions.md](discovery/open-questions.md#still-open)), so the lobby's address is a setting in the
game and the first address is the one Fly.io gives the app.

The owner accepted none of this yet. It is a recommendation, and every part can change later without a change to
the game's design.

## Why

### What decides it

- **The relay carries little.** Only inputs cross the network. An estimate (INFERRED, not measured): a relayed
  player sends about 2 KB a second and receives about 6 KB a second in a 16-player session, with packet headers.
  That is about 30 MB an hour.
- **The relay's place matters.** Every relayed packet travels to the relay and on to the other player. A relay far
  from both players adds delay to every input.
- **The services are needed from phase 4.** Until then nothing should cost money or need care.
- **One person runs it.** A server that faces the internet needs updates and watching. Less of that is better.
- **Others must be able to run their own.** The lobby stays an ordinary program in a container, with no code that
  only one provider can run.

### The relay

| Option | Price | UDP | Places | Work to run |
| --- | --- | --- | --- | --- |
| **Cloudflare Realtime TURN** | 1,000 GB a month free, then $0.05 per GB | Yes: ports 3478 and 443 | Cloudflare's whole network; a client reaches the nearest location | None |
| coturn on a Hetzner cloud server | From €5.49 a month; 20 TB a month included at the EU locations, 1 TB in the US | Yes | One location per server | Install, update and watch a public server |
| coturn on Fly.io | $2.19 a month for the smallest machine, $2 for the IPv4 address that UDP needs, $0.02 per GB sent in Europe and North America | Yes, IPv4 only, with a special bind address | One per machine | A relay needs a wide port range, which fits Fly.io's UDP service badly (INFERRED) |

Cloudflare's free 1,000 GB is about 33,000 hours of relayed play a month at the estimate above. Most players will
not need the relay at all. The service follows the TURN standard (RFC 8656) over UDP, and its relay addresses are
IPv4 only. The lobby asks Cloudflare for a short-lived user name and password per player; the long-lived key stays
on the lobby.

Not verified: that libjuice, the library chosen for NAT traversal, works with Cloudflare's service. Phase 4 tests
it first. coturn on a server stays the fallback, and the lobby's reply has the same shape for both, so the game
does not change.

### The lobby server

| Option | Price | Fit |
| --- | --- | --- |
| **Fly.io, one small machine that stops when idle** | $2.19 a month while running; a stopped machine costs $0.15 per GB of its disk image a month, and the image is 8 MB. No free allowance | Runs the container as it is. HTTPS comes with it. The lobby needs no UDP |
| Cloudflare Workers | Free: 100,000 requests a day | The lobby would have to be written for that one platform, which breaks "others must be able to run their own" |
| A Hetzner cloud server | From €5.49 a month, always | Also right if the fallback relay is ever needed; then the lobby moves there too |
| The owner's own machine | Free | Needs a public address and uptime; not for a public service |

A stopped lobby loses nothing: a session exists only while its host stays connected, and a connected host keeps
the machine running.

### Downloads

GitHub Releases holds the builds, as it does for Glovebox. Builds contain no ROM data
([discovery/repository.md](discovery/repository.md#what-may-be-distributed)). A release also needs weemitt's
license, which is still open.

## What is set up today

- **The lobby server placeholder** in [`server/`](../server/README.md): a program that answers a health check and
  describes itself. It lists no sessions yet.
- **Its container image**, built by `make image` from `server/Dockerfile`.
- **CI** (`.github/workflows/ci.yml`): on every pull request it tests the placeholder, builds the image, runs it
  and asks it for its health. On every push to `main` it also publishes the image to ghcr.io.
- **The Fly.io settings** in `server/fly.toml`. They hold no secret.
- **A deploy workflow** (`.github/workflows/deploy-lobby.yml`), started by hand. It fails with a clear message
  until its token exists.

Nothing runs on Fly.io or Cloudflare yet. Both need an account that only the owner can open.

## What the owner has to do

To put the placeholder online (optional before phase 4):

1. Open a Fly.io account and install its `flyctl` tool.
2. In `server/`, create the app from the settings in the repository. If the name is taken, pick another and commit
   the changed `fly.toml`.

   ```bash
   fly launch --copy-config --no-deploy
   ```

3. Make a deploy token and save it as the repository secret `FLY_API_TOKEN` (Settings, Secrets and variables,
   Actions).

   ```bash
   fly tokens create deploy
   ```

4. Run the "Deploy the lobby" workflow from the repository's Actions page.
5. Check it. The address is the one `fly launch` printed.

   ```bash
   curl https://g64coopdx-lobby.fly.dev/v1/info
   ```

For the relay, in phase 4:

1. Open a Cloudflare account and create a TURN key under Realtime.
2. Give the lobby the key's id and its API token as Fly.io secrets, never in a file.

   ```bash
   fly secrets set TURN_KEY_ID=... TURN_KEY_API_TOKEN=...
   ```

The placeholder does not read these two settings yet. Phase 4 adds the code that does.

## Rules for the services

- No secret goes into the repository, a container image or a workflow file. Secrets live in Fly.io's secrets and in
  GitHub's repository secrets.
- The lobby stores no personal data beyond what a session list needs while the session is open.
- The lobby and the relay never carry or store game data.
- The game takes the lobby's address from a setting. Anyone can run the same container and point the game at it.
