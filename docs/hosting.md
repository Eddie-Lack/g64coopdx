# Hosting

This file says where g64coopdx's online services and downloads run, why, what is set up today, and how to set it up.
The owner is the person who runs the project. Prices and limits were read from each provider's own pages on
14 November 2026. Check them again before paying for anything. A claim marked INFERRED is concluded from what was
read or measured, not read or measured itself ([discovery/README.md](discovery/README.md)).

"Hosting" here means the project's own services. Who hosts a game session is a different question: one player's
machine does ([discovery/netcode.md](discovery/netcode.md#recommendation-b-with-host-confirmed-inputs)).

## What needs hosting

| Thing | What it does | Needed from |
| --- | --- | --- |
| The lobby server | Lists open sessions; passes connection details between a host and a joiner; hands out relay credentials. It speaks HTTPS and never sees game data | Now: the relay and the list of open games. The rest in phase 4 |
| STUN | Tells a machine its public address | Phase 4 |
| The relay (TURN) | Carries the game's packets when two machines cannot reach each other directly | Now, as the lobby's own relay (below) |
| Downloads | The released Windows and Linux builds | Phase 6 |
| The source and CI | The repository and its tests | Now |

The plan had the first playable versions (phases 1 to 3) connect directly, with no service
([discovery/roadmap.md](discovery/roadmap.md)). The relay came before phase 4, on 15 November 2026. The lobby and its
relay run now ("The decision", below).

## The decision

| Thing | Where it runs | Cost while idle |
| --- | --- | --- |
| The lobby server | A small container on Fly.io that keeps running (the relay needs it) | About $2.19 a month |
| STUN | Cloudflare's public STUN server | Free |
| The relay | For now: the lobby's own UDP relay on Fly.io, with rooms joined by code (`server/relay.go`). Later, if it carries much: Cloudflare Realtime TURN | $2 a month for the dedicated IPv4 address that UDP needs, and $0.02 per GB |
| Downloads | GitHub Releases | Free |
| The lobby's container image | The GitHub container registry (ghcr.io) | Free |
| The source and CI | GitHub and GitHub Actions | Free |

No domain name is bought for the project. Its name is not chosen yet; `g64coopdx` is the working name
([discovery/open-questions.md](discovery/open-questions.md#still-open)). The lobby's address is a setting in the
game. Until the name is chosen, the address is `lobby.slugs.io`, a subdomain of the owner's existing domain. The
address that Fly.io gives the app, `g64coopdx-lobby.fly.dev`, also works.

The owner left this decision to the discovery report ([discovery/README.md](discovery/README.md)). Every part can
change later without a change to the game's design.

The relay changed on 15 November 2026: the owner asked for online play without port forwarding before phase 4. The
lobby's own relay needs no Cloudflare account and no NAT traversal in the game: every player sends to one UDP port,
and the relay passes each packet on inside its room. At the estimate below (about 30 MB an hour per relayed player)
an hour of a two-player game costs well under a cent. Cloudflare's TURN service, or direct connections through
libjuice, the library chosen for NAT traversal, can replace it in phase 4.

## Why

### What decides it

- **The relay carries little.** Only inputs cross the network. An estimate (INFERRED, not measured): a relayed
  player sends about 2 KB a second and receives about 6 KB a second in a 16-player session, with packet headers.
  That is about 30 MB an hour.
- **The relay's place matters.** Every relayed packet travels to the relay and on to the other player. A relay far
  from both players adds delay to every input.
- **The services are needed from phase 4.** Until then nothing should cost money or need care. The relay came
  earlier, at the owner's request (above).
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

Not verified: that libjuice works with Cloudflare's service. Phase 4 tests it first. coturn on a server stays the
fallback, and the lobby's reply on `/v1/ice` has the same shape for both, so the game does not change.

### The lobby server

| Option | Price | Fit |
| --- | --- | --- |
| **Fly.io, one small machine** | $2.19 a month while running; a stopped machine costs $0.15 per GB of its disk image a month, and the image is 8 MB. No free allowance | Runs the container as it is. HTTPS comes with it. The relay's UDP needs a dedicated IPv4 address, $2 a month |
| Cloudflare Workers | Free: 100,000 requests a day | The lobby would have to be written for that one platform, which breaks "others must be able to run their own" |
| A Hetzner cloud server | From €5.49 a month, always | Also right if the fallback relay (coturn on a server) is ever needed; then the lobby moves there too |
| The owner's own machine | Free | Needs a public address and uptime; not for a public service |

The first plan let the machine stop when idle. A stopped lobby lost nothing: a session existed only while its host
stayed connected, and a connected host kept the machine running. With the relay, the machine keeps running
(`server/fly.toml`). The relay's UDP packets do not start a stopped machine, and a game must not lose its relay
halfway through.

### Downloads

GitHub Releases holds the builds, as it does for Glovebox, the level editor. Builds contain no ROM data
([discovery/repository.md](discovery/repository.md#what-may-be-distributed)). weemitt's code in them is under
weemitt's MIT license.

## What is set up today

- **The lobby server** in [`server/`](../server/README.md): a program that answers a health check, describes
  itself, and runs the relay on UDP port 27065. The relay keeps the list of open games that hosts chose to show. The
  program is built with a supported Go release (1.27 in `server/Dockerfile` and in CI).
- **Its container image**, built by `make image` from `server/Dockerfile`.
- **CI** (`.github/workflows/ci.yml`): on every pull request it tests the lobby server, builds the image, runs it
  and asks it for its health. On every push to `main` it also publishes the image to ghcr.io.
- **The Fly.io settings** in `server/fly.toml`. They hold no secret.
- **A deploy workflow** (`.github/workflows/deploy-lobby.yml`). It deploys on a push to `main` that changes
  `server/`, or when started by hand.

The lobby runs on Fly.io at `https://lobby.slugs.io`. The relay answers at `g64coopdx-lobby.fly.dev:27065`, on the
app's dedicated IPv4 address (below).

## Setting up the lobby

The lobby is online, with its relay. These are the steps that put them there, for reference. The steps for
Cloudflare's relay, at the end, are for later.

The owner's Fly.io account already runs another project, slugs-game. The lobby is a second app in that account. An
app has its own machine, address, secrets and bill line. The two projects do not touch each other.

1. Create the app. If the name is taken, pick another, and commit the new name in `server/fly.toml`. The game's
   default relay (`NET_RELAY_DEFAULT` in `src/client/netplay.h`) names the app too.

   ```bash
   fly apps create g64coopdx-lobby
   ```

2. Make a deploy token for this app only.

   ```bash
   fly tokens create deploy -a g64coopdx-lobby
   ```

3. Save the token as the secret `FLY_API_TOKEN` of the g64coopdx repository (Settings, Secrets and variables,
   Actions). slugs-game keeps its own token; a deploy token works for one app.
4. Run the "Deploy the lobby" workflow from the repository's Actions page. From then on, every push to `main` that
   changes `server/` deploys.
5. Give the app the subdomain. Fly.io prints the DNS records it wants.

   ```bash
   fly certs add lobby.slugs.io -a g64coopdx-lobby
   ```

6. Add those records at slugs.io's DNS (Cloudflare): an A and an AAAA record named `lobby`, set to "DNS only".
   Cloudflare's proxy stops Fly.io from issuing the certificate.
7. Check it.

   ```bash
   curl https://lobby.slugs.io/v1/info
   ```

To let others run the lobby's image: open the package `g64coopdx-lobby` on GitHub, then Package settings, Change
visibility, Public. A new package is private, and a public one cannot be made private again.

For the lobby's relay, once:

1. Give the app a dedicated IPv4 address. Fly.io delivers UDP only to one. It costs $2 a month.

   ```bash
   fly ips allocate-v4 -a g64coopdx-lobby
   ```

2. The next deploy (any push to `main` that changes `server/`, or the "Deploy the lobby" workflow) adds the UDP
   service and keeps the machine running. `g64coopdx-lobby.fly.dev` then points at the new address too. The name also
   keeps Fly.io's shared IPv4 address, which drops UDP. The program sends to each of the relay's addresses until one
   answers, then keeps that one.
3. Optional: point `lobby.slugs.io` at it. Replace the `lobby` A record at slugs.io's DNS with the new address that
   `fly ips list -a g64coopdx-lobby` shows, still "DNS only". The game's default relay is `g64coopdx-lobby.fly.dev`,
   so this is not needed.

For Cloudflare's relay, if the lobby's own one ever carries too much:

1. Open a Cloudflare account and create a TURN key under Realtime.
2. Give the lobby the key's id and its API token as Fly.io secrets, never in a file.

   ```bash
   fly secrets set TURN_KEY_ID=... TURN_KEY_API_TOKEN=...
   ```

The lobby server does not read these two settings yet. Phase 4 adds the code that does.

## Rules for the services

- No secret goes into the repository, a container image or a workflow file. Secrets live in Fly.io's secrets and in
  GitHub's repository secrets.
- The lobby stores no personal data beyond what a session list needs while the session is open.
- The lobby and the relay never carry or store game data: anything from the ROM, or made from it.
- The game takes the lobby's address from a setting. Anyone can run the same container and point the game at it.
