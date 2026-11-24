# sm64coopdx: what it is and how it works

sm64coopdx is an online multiplayer version of the Super Mario 64 PC port. This file says what it does and how, and
what carries over to a Glover co-op game. It answers question 1 of the discovery. The discovery is the study that
[README.md](README.md) reports, and its section "The documents" lists the questions. Its
[Terms](README.md#terms) define the words used here, such as rollback, instance, start record, input log, NAT
traversal and ICE.

## How this was studied

- Source: a shallow clone of github.com/coop-deluxe/sm64coopdx at commit `8cd6e5977d9f920d51ca71f2c61801d019ed79c6`
  (31 May 2026), read only. Nothing from it was built or run. Files outside the clone, on other branches and in
  other repositories (section 1), were read as raw files on raw.githubusercontent.com.
- Paths are relative to that repository's root, with line numbers where they help.
- **VERIFIED** means read in the source or its docs. **INFERRED** means concluded from what was read. **UNKNOWN**
  means not found.
- No code is copied. Only identifiers (file, function and constant names) are quoted.
- Limits: the clone has one commit, so its history and merges are not visible. GitHub's web pages and API were
  blocked during the study, so its issues and most of its wiki were not read.

## Summary

| Question | Answer |
| --- | --- |
| License | No license file at the root (VERIFIED). No one has a written right to copy its code. It may be studied, not copied. Its lobby server, CoopNet, is AGPL-3.0 (VERIFIED) |
| Players | Up to 16 (`MAX_PLAYERS`, `include/types.h:602`, VERIFIED); the host can set fewer |
| Connection | Direct UDP to the host on port 7777, with no NAT traversal; or CoopNet, a lobby and signaling server, after which players most likely connect peer to peer with ICE (libjuice; INFERRED, section 2.3) |
| Network model | State replication. Each machine runs its own copy of the level. A machine sends its own Mario and the objects it owns. The nearest player owns an object. No lockstep, no rollback, no tick numbers on the wire |
| Tick | 30 Hz game logic (`src/pc/pc_main.c:91`, VERIFIED) |
| Late join | A joiner downloads the host's mods, gets the host's save, then asks a player in the same area for that area's state |
| Different levels | Allowed. Packets carry course, act, level and area; players in other areas are filtered out |
| Desync | Logged and partly repaired by "forgetting" broken objects (section 3.8). No full resync |
| Mods | Lua 5.3, about 2,000 functions generated from C headers, 69 hook types, sync tables (section 4.4), DynOS for models, levels and textures (section 4.7). The host's mods download to every joiner, with an MD5 cache and a 35 MB cap per mod |
| Tests | CI builds five platforms. It runs no tests (VERIFIED) |

## What carries over to Glover

| sm64coopdx feature | For Glover | Why |
| --- | --- | --- |
| Up to 16 players | Carries over, with work | The owner wants 8 or more. Glover's frame limits (256 object matrices, the display list buffer, 800 vertices) need raising for 16 gloves and 16 balls: see [state-inventory.md](state-inventory.md) |
| Direct connection plus a lobby and signaling server | Carries over | The same split fits: a lobby that only introduces players, then peer to peer with a relay as fallback. g64coopdx must not use CoopNet's code (AGPL, and another port's code). It can use libjuice from the libjuice project itself (MPL-2.0, section 1.2) or Valve's GameNetworkingSockets: see [netcode.md](netcode.md). The copy of libjuice to avoid is sm64coopdx's prebuilt one (section 1.3) |
| A network layer behind a table of functions (`struct NetworkSystem`) | Carries over as a design idea | It lets the direct mode and the lobby mode share all game code |
| State replication with nearest-player ownership | Does not fit as well | Glover's logic is bit-exact (the same inputs give the same state, bit for bit, on every machine) and its savestates cost about 0.02 ms, which makes rollback cheap. Rollback needs no per-object sync code. sm64coopdx needs thousands of lines of it: see [netcode.md](netcode.md) |
| Players in different levels | Carries over, within one world | The owner chose it. Each occupied level is its own rollback simulation (an instance), and packets belong to an instance, as sm64coopdx's carry a level and area: see [netcode.md](netcode.md#instances) |
| Late join from a peer in the same area | Changes | With rollback a joiner replays the instance's inputs from its start; the host keeps every instance's start record and input log |
| Exact version string match, shown in the lobby list | Carries over | A rollback game needs the same build on every machine even more |
| The host's save is the session's save | Carries over | Progress belongs to the host's save; clients keep their own saves untouched |
| Lua with per-mod environments, no `io`, `os`, `load` | Carries over, stricter | In a rollback game a script must also be deterministic and its state must be in the savestate: see [mods.md](mods.md) |
| Sync tables | Not needed | In a rollback game every machine runs the same scripts on the same inputs. Shared script state comes for free |
| Bindings generated from C headers | Carries over as a design idea | It keeps the Lua API and its docs in step |
| Mods sent to joiners, MD5 cache, 800-byte chunks, 35 MB cap | Carries over, with SHA-256, a prompt and path checks | sm64coopdx runs downloaded mods without asking and its path checks are unclear (section 4.9) |
| DynOS (custom models, levels, textures as compiled packs) | Replaced by Glovebox | Glovebox already writes levels and models in the game's own formats: see [mods.md](mods.md) |
| Chat, player list, nametags, kick, ban, moderators with a confirm step | Carries over | Plain features; nothing game-specific |
| Five characters and eight-part palettes | Partly | Glover has one glove model; colors and simple tints carry over: see [README.md](README.md#player-presentation-and-the-interface) |
| Discord rich presence | Optional | The Discord Game SDK is archived ([netcode.md](netcode.md#transport-and-services)) |
| Build-only CI | Do better | weemitt's recorded runs and replays (`tools/regress.py`, `--record`, `--replay`) already give the base for headless network tests |

## What g64coopdx took from it

- The lobby and signaling split, and a network layer behind a function table: [netcode.md](netcode.md#transport-and-services).
- The exact version handshake, the host's save as the session's save, and moderation with a confirm step:
  [netcode.md](netcode.md#the-handshake) and [README.md](README.md).
- Stable object ids from the order of the level's load, plus id blocks per player for spawned objects. They are
  used in [netcode.md](netcode.md) for option C, state sync. Rollback, the model that netcode.md recommends, does not
  need them.
- Mod delivery by manifest and hash, and its gaps (no prompt, unclear path checks, bytecode accepted):
  [mods.md](mods.md#how-mods-reach-other-players).
- The player-index trap (indices differ per machine): [mods.md](mods.md#the-lua-api).
- Its history as a fork of a fork that stopped following its upstream (section 5): [weemitt.md](weemitt.md#how-g64coopdx-relates-to-weemitt).

## Details


### 1. License

#### 1.1 What the repo says

- VERIFIED: there is no top-level LICENSE, COPYING or NOTICE file. `git ls-files | grep -i licen` finds only:
  - `src/pc/utils/miniz/LICENSE`
  - `tools/ido5.3_compiler/LICENSE.md`
  - `tools/n64graphics_ci_dir/LICENSE`
  - `tools/sm64tools.LICENSE`
- VERIFIED: raw.githubusercontent.com returns 404 for `LICENSE`, `LICENSE.md` and `COPYING` on branches `main`, `master` and `dev` of coop-deluxe/sm64coopdx.
  The same files return 404 for djoslin0/sm64ex-coop (branch `coop`), sm64pc/sm64ex (branch `nightly`) and sm64-port/sm64-port (branch `master`).
  The 404s for the other repos are VERIFIED. INFERRED: the N64 decompilation line of projects has no license file, and these 404s match that.
- VERIFIED: README.md (lines 1-22) and credits.txt contain no license statement.
- Conclusion: the license of sm64coopdx as a whole is UNKNOWN. No grant of rights is written down in the repo.

#### 1.2 Per-component licenses found

| Component | Where | License | Status |
| --- | --- | --- | --- |
| miniz (zip/deflate helper) | `src/pc/utils/miniz/LICENSE` (lines 1-20) | MIT-style ("Permission is hereby granted, free of charge..."), copyright RAD Game Tools, Valve Software, Rich Geldreich | VERIFIED |
| sm64tools | `tools/sm64tools.LICENSE` line 1 | MIT, copyright 2015-2018 queueRAM | VERIFIED |
| n64graphics | `tools/n64graphics_ci_dir/LICENSE` | MIT, copyright 2019 David Benepe | VERIFIED |
| IDO 5.3 compiler (recompiled) | `tools/ido5.3_compiler/LICENSE.md` | Not read in detail (tool only, not game code) | UNKNOWN content |
| n64-fast3d renderer (Emill) | `src/pc/gfx/README-n64-fast32-engine.md` lines 7-20 | Custom BSD-like text: "Redistribution and use in source forms, with or without modification... Redistributions in binary form are not allowed." | VERIFIED |
| Lua 5.3.5 headers | `lib/lua/include/lua.h` lines 1-20 ("Lua 5.3.5", Lua.org, PUC-Rio) | Lua is MIT. The clone ships only headers and prebuilt libs per platform (`lib/lua/linux`, `win64`, `mac_arm`, and so on). | VERIFIED version, INFERRED license (MIT is Lua's known license) |
| libcoopnet client | `lib/coopnet/` : `libcoopnet.a`, `libjuice.a` (and .dylib on macOS), header `include/libcoopnet.h`, `README.md` | Prebuilt binaries only in this repo. `lib/coopnet/README.md` says: "Source code for coopnet is available at https://github.com/coop-deluxe/coopnet". | VERIFIED |
| coopnet repo (separate) | https://raw.githubusercontent.com/coop-deluxe/coopnet/master/LICENSE | GNU Affero General Public License v3 (full AGPL text, "Version 3, 19 November 2007"). The repo README is one line: "Signaling server / lobby manager for P2P connections". | VERIFIED (branch `master`, not `main`) |
| libjuice | `lib/coopnet/linux/libjuice.a`, `lib/coopnet/mac_*/libjuice.1.6.2.dylib` | Not stated in this repo. The libjuice project itself is MPL-2.0 (https://raw.githubusercontent.com/paullouisageneau/libjuice/master/LICENSE) | VERIFIED for the project; version 1.6.2 per the dylib file name |
| Discord Game SDK / discord-rpc | `lib/discordsdk/`, `lib/discord/libdiscord-rpc.*`, `src/pc/discord/discord_game_sdk.h` | Proprietary Discord SDK; no license file in repo | UNKNOWN |
| Game assets and code | whole tree | Derived from Nintendo's Super Mario 64 (decompilation) | VERIFIED derivation (credits.txt line 83) |

#### 1.3 What this means for a project that studies but does not copy

- INFERRED: no license means default copyright applies. Nobody has a written right to copy or adapt the code. The code is also a derivative of decompiled Nintendo code, so even the authors may have no clean grant to give.
- INFERRED: studying the design, reading the docs, and writing your own implementation is safe from a copyright view. Ideas, protocols as concepts, and architecture are not copyrightable.
  Copying code, structures with their exact layout and field order, or text of the docs is not safe.
- INFERRED: the coopnet server is AGPL-3.0. A project that reads its code and then writes a lobby server must keep the rules in its own words. Running a copy of it, changed or not, as a network service obliges the operator to offer its source to the users of that service (AGPL section 13). Reading its README or the header `lib/coopnet/include/libcoopnet.h` for the idea of a lobby API is low risk. Do not copy the header's declarations.
- INFERRED: bundling their prebuilt `libcoopnet.a` / `libjuice.a` into g64coopdx would raise AGPL questions (the lib is built from AGPL source, the license of the client library itself is UNKNOWN). Avoid it. Write your own relay and lobby, or use a library with a clear license.
- VERIFIED: weemitt's rules file, CLAUDE.md, already forbids code from other ports, and g64coopdx applies the same rule. Study notes like these are fine; code is not.
- Their own rule on external code: their repo has no CONTRIBUTING/license policy file that this study found (UNKNOWN).

---

### 2. Features

#### 2.1 Player count

- VERIFIED: `include/types.h:602` `#define MAX_PLAYERS 16`. This is a compile-time constant used for every per-player array (network players, Mario states, packet receive tables, sync tables).
- VERIFIED: the host can set a lower limit. `src/pc/network/network.h` struct ServerSettings has `maxPlayers`; `network.c:89` defaults it to MAX_PLAYERS; `network.c:137` takes it from `configAmountOfPlayers`; CLI `--playercount` sets it (`src/pc/cliopts.c:32`).
- VERIFIED: join refusal when full: `packets/packet_join.c` network_send_join sends `EKT_FULL_PARTY` if no free slot or `connectedCount >= maxPlayers`.
- VERIFIED: the player index space is 0 to 15. Slot 0 is always the local player, slot with `type == NPT_SERVER` is the host (`network_player.h`: NPT_UNKNOWN, NPT_LOCAL, NPT_SERVER, NPT_CLIENT).
- Lesson: size every per-player table with one constant from day one. Their docs warn that player indices differ per machine (section 4.4).

#### 2.2 Direct connection

- VERIFIED: two network systems are selectable at runtime. `network.h` `enum NetworkSystemType { NS_SOCKET, NS_COOPNET, NS_MAX }` and `struct NetworkSystem` (function table: initialize, get_id, get_id_str, save_id, clear_id, dup_addr, match_addr, update, send, get_lobby_id, get_lobby_secret, shutdown, plus a `requireServerBroadcast` flag and `name`).
- VERIFIED: the socket system (`src/pc/network/socket/socket.c`, `socket_linux.c`, `socket_windows.c`) is plain UDP over a single IPv6 dual-stack socket (`sockaddr_in6`; IPv4 addresses are mapped into `::ffff:`). Default port `DEFAULT_PORT 7777` (`src/pc/configfile.h:16`).
  The host listens; the joiner types an IP or host name. Host name is resolved with `getaddrinfo` (`resolve_domain`). Square-bracketed IPv6 is accepted.
- VERIFIED: the socket system sets `requireServerBroadcast = true` (`socket.c:292`). Clients talk only to the host. The host forwards (see 3.1).
- VERIFIED: UI panels for this: `src/pc/djui/djui_panel_join_direct.c`, `djui_panel_host.c`, `djui_panel_host_settings.c`, `djui_panel_host_mods.c`, `djui_panel_host_save.c`.
- NAT traversal for direct mode: none in the code read for this study. The host must forward port 7777 (INFERRED from the absence of any hole-punching code in `socket/`).
- CLI: `--server PORT`, `--client IP PORT`, `--coopnet PASSWORD`, `--headless`, `--playername`, `--playercount` (`src/pc/cliopts.c:18-39`).
- Headless dedicated server: VERIFIED. `--headless` (`cliopts.c:37`, `pc_main.c:220`) plus `gServerSettings.headlessServer` (set in `network.c:140` only for server role). In `packets/packet_player.c` network_update_player the headless host sends no player packet of its own.

#### 2.3 CoopNet (relay/lobby service)

What it is (VERIFIED facts first):
- `src/pc/network/coopnet/coopnet.c` (320 lines) is the second `NetworkSystem`, named "CoopNet" (`coopnet.c:317`). It wraps the client library `libcoopnet`, shipped only as a binary (prebuilt in `lib/coopnet/`).
- Build switch: `COOPNET ?= 1` in the Makefile (line 53); forced to 0 on some targets (Makefile around line 313). `#ifdef COOPNET` guards the system in `network.c:104`.
- Default server: `DEFAULT_COOPNET_IP "net.coop64.us"` and `DEFAULT_COOPNET_PORT 34197` (`src/pc/configfile.h:17-18`). Both are config values (`coopnet_ip`, `coopnet_port`, `coopnet_password`, `coopnet_dest` in `configfile.c:388-392`), so a user can point the game at another server.
- Hosted by the project: INFERRED yes, from the default host name. Who owns that domain is UNKNOWN. The server source is public at github.com/coop-deluxe/coopnet (AGPL-3.0, section 1.2), described as "Signaling server / lobby manager for P2P connections".
- The server can redirect clients: callback `OnLoadBalance(host, port)` rewrites the saved coopnet host and port (`coopnet.c` coopnet_on_load_balance). VERIFIED. So the project can move load between servers.

How the library is used (VERIFIED from `lib/coopnet/include/libcoopnet.h`, identifiers only):
- Session calls: `coopnet_begin(host, port, name, destId)`, `coopnet_update`, `coopnet_shutdown`, `coopnet_is_connected`.
- Lobby calls: `coopnet_lobby_create(game, version, hostName, mode, maxConnections, password, description)`, `coopnet_lobby_update`, `coopnet_lobby_join(lobbyId, password)`, `coopnet_lobby_leave`, `coopnet_lobby_list_get(game, password)`.
- Data calls: `coopnet_send`, `coopnet_send_to(peerId, ...)`, `coopnet_unpeer`.
- Callbacks: OnConnected, OnDisconnected, OnLobbyCreated, OnLobbyJoined, OnLobbyLeft, OnLobbyListGot, OnLobbyListFinish, OnReceive, OnError, OnPeerConnected, OnPeerDisconnected, OnLoadBalance, DestIdFunction.
- Error enum `MPacketErrorNumber`: lobby not found, lobby full, join failed, password incorrect, coopnet version mismatch, peer failed.
- IDs are 64-bit ("userId", "lobbyId", "destId").

How lobbies work (VERIFIED in `coopnet.c`):
- A lobby entry carries: game name (`GAME_NAME` = "sm64coopdx", "sm64coopdx-dev" or "sm64coopdx-intl", `src/pc/network/version.h`), version string, host player name, a "mode" string (name of the main enabled mod, `mods_get_main_mod_name`), max connections, optional password, and a description of up to 1024 characters (`MAX_COOPNET_DESCRIPTION_LENGTH`). The description is the version plus the list of enabled mod names (`coopnet_populate_description`).
- Host: connect to coopnet, then `coopnet_lobby_create` on first update (`ns_coopnet_update`). On a rehost it calls `coopnet_lobby_update` instead.
- Joiner: connect, `coopnet_lobby_join(gCoopNetDesiredLobby, gCoopNetPassword)`. After `OnLobbyJoined` a client immediately sends a mod list request (`network_send_mod_list_request`, `coopnet.c` coopnet_on_lobby_joined). So the game's own handshake runs inside the lobby.
- Invites: lobby id string is "coopnet:<lobbyId>", secret "coopnet:<lobbyId>:<password>" (`ns_coopnet_get_lobby_id`, `..._get_lobby_secret`). Used for Discord invites (2.9). A password-protected lobby only gives an invite secret to the owner (`coopnet_allow_invite`).
- Peer identity: each remote player has a 64-bit userId plus a "destId" that stays the same across sessions (`coopnet_id.c`: `coopnet_save_dest_id`, `coopnet_get_dest_id`; config `coopnet_dest`). The ban and moderator lists use the destId string (`ns_coopnet_get_id_str`). So bans persist across rejoin.

NAT traversal:
- VERIFIED: the repo ships `libjuice` next to `libcoopnet` in `lib/coopnet/{linux,win32,win64,mac_arm,mac_intel}`. The callbacks include OnPeerConnected, OnPeerDisconnected and the error MERR_PEER_FAILED ("peer failed").
- INFERRED: libjuice is an ICE/STUN peer-to-peer connection library. With the one-line coopnet README ("Signaling server / lobby manager for P2P connections") this means: the coopnet server only matches players and passes connection setup messages (signaling). The game packets then go peer to peer through ICE hole punching. Evidence for P2P data path: the CoopNet system has `requireServerBroadcast = false` (`coopnet.c:316`), and `coopnet_send_to(peerId, ...)` addresses individual peers.
- UNKNOWN: whether a TURN-style relay fallback exists when hole punching fails. The client shows "peer failed" for that player (`coopnet_on_error`, case MERR_PEER_FAILED). This study found no relay code in the repo.
- Result in the game: with CoopNet there is still a logical host (the player who created the lobby, see `coopnet_on_lobby_joined`), but clients also send to each other directly (so "broadcast" goes to every peer).

#### 2.4 Lobby browser

- VERIFIED: `src/pc/djui/djui_panel_join_lobbies.c` and `djui_lobby_entry.c`. It calls `ns_coopnet_query(callback, finishCallback, password)` which calls `coopnet_lobby_list_get(GAME_NAME, password)` (`coopnet.c` top).
- VERIFIED: an entry is shown disabled when its version string differs from the local `get_version()` (`djui_panel_join_lobbies.c:89-91`, `strcmp(version, aVersion) != 0`). So the version check also happens before join, in the browser.
- VERIFIED: private lobbies are reached through `djui_panel_join_private.c` (password query / direct lobby id).
- The listing filters by game name, so "sm64coopdx", "-dev" and "-intl" builds do not see each other (VERIFIED: GAME_NAME passed as filter; `version.h` defines three names).

#### 2.5 Chat

- VERIFIED: `packets/packet_chat.c`; `src/pc/djui/djui_chat_box.c`, `djui_chat_message.c`. Max length `MAX_CHAT_MSG_LENGTH 500` (`djui_chat_message.h:4`).
- VERIFIED: anti spoof: `packet_chat.c:89-91` calls `packet_spoofed(p, globalIndex)` and drops the message when the sender index does not match the connection (only effective when `requireServerBroadcast` is false, see `packet.c` packet_spoofed).
- VERIFIED: chat commands start with `/` (`src/pc/chat_commands.c`): `/players`, `/kick`, `/ban`, `/permban`, `/moderator`, `/confirm`, `/nametags`. Mods add commands through `hook_chat_command` (docs/lua/guides/hooks.md).
- VERIFIED: kick, ban and moderator actions need a `/confirm` step (`enum ChatConfirmCommand` in `packets/packet.h`: CCC_NONE, CCC_KICK, CCC_BAN, CCC_PERMBAN, CCC_MODERATOR).

#### 2.6 Player list

- VERIFIED: `src/pc/djui/djui_panel_playerlist.c` and `gServerSettings.enablePlayerList` and `enablePlayersInLevelDisplay` (`network.h`). Ping is tracked per player (`struct NetworkPlayer.ping`, `network_player.h`). Each `NetworkPlayer` has a name, `description` (20 chars max, `MAX_DESCRIPTION_STRING 20`) with RGBA color, and `overrideLocation` string that mods can set (Lua `network_player_set_description`, `network_player_set_override_location`).
- Nametags: `gServerSettings.nametags`, `struct NametagsSettings { showHealth, showSelfTag }`, `/nametags` command.

#### 2.7 Player models and palettes

- VERIFIED: five built-in characters, `src/game/characters.h`: CT_MARIO, CT_LUIGI, CT_TOAD, CT_WALUIGI, CT_WARIO.
- VERIFIED: palette is 8 RGB parts, `src/game/player_palette.h`: PANTS, SHIRT, GLOVES, SHOES, HAIR, SKIN, CAP, EMBLEM (`PLAYER_PART_MAX`). Packed struct (`#pragma pack(1)`), so it goes on the wire as raw bytes. Preset palettes are `.ini` files in `palettes/` (Mario.ini, Luigi.ini, Toad.ini, Waluigi.ini, Wario.ini).
- VERIFIED: model and palette are sent in the join request (`packets/packet_join.c`: model u8, palette struct, name string) and again in `PACKET_PLAYER_SETTINGS` (`packets/packet_player_settings.c`). A mod can override another player's model/palette locally (`overrideModelIndex`, `overridePalette` in `struct NetworkPlayer`).
- Custom player models come from DynOS (`.bin` actor files) and Lua (docs/lua/lua.md links "Custom Player Model" example `examples/koopa-player-model`).

#### 2.8 Moderation

- VERIFIED: ban list: `src/pc/network/ban_list.c/.h` (`ban_list_add(address, perm)`, `ban_list_contains`). The key is the network id string (IP for socket, destId for CoopNet).
  Checked on every received packet by the host in `packets/packet.c` packet_receive: a banned sender is sent `EKT_BANNED` and the packet is dropped.
- VERIFIED: `/permban` = permanent flag (`gBanPerm`); `/ban` = this session only (`ban_list_add(..., false)` in `packet_command_mod.c`).
- VERIFIED: moderators: `src/pc/network/moderator_list.c/.h`, `gNetworkPlayers[i].moderator`, packet `PACKET_MODERATOR`. A moderator can kick and ban but only by asking the host: `network_send_chat_command` sends `PACKET_COMMAND` to the host, the host checks `moderator_list_contains` (`packet_command_mod.c:26`), accepts only CCC_KICK and CCC_BAN, then kicks. A moderator cannot make moderators (host only: the `/moderator` command exists in `chat_commands.c:203`; host-only gate is INFERRED).
- VERIFIED: kick packet `PACKET_KICK` with reasons `EKT_CLOSE_CONNECTION, EKT_FULL_PARTY, EKT_KICKED, EKT_BANNED, EKT_REJOIN` (`packets/packet.h`). The client only honors a kick that comes from the server player (`packet_kick.c` network_receive_kick: "Kick came from non-server... refuse!"). `EKT_REJOIN` makes the client reconnect (the rehost flow, 2.10).
- VERIFIED: an automatic anti-cheat check: in release builds, a player packet whose action is `ACT_DEBUG_FREE_MOVE` gets the sender kicked (`packet_player.c` network_receive_player). In `DEVELOPMENT` builds it only shows a popup.
- VERIFIED: server-side permissions are limited. Server settings (`ServerSettings`) are pushed to clients in the join packet: player interactions (none, solid, PvP), bouncy level bounds, knockback strength, stay-in-level after star, skip intro, bubble death, nametags, max players, pause anywhere, PvP type.
- VERIFIED: rate limiting of sent packets: `MAX_PACKETS_PER_SECOND_PER_PLAYER ((u16)100)` in `network.c:59`; the sender drops packets above the cap per second (`network_send_to`). This limits only outgoing traffic, not incoming. Incoming flood protection: UNKNOWN (not found).
- Duplicate-packet filter: per player table of 256 `(seqId, hash)` pairs (`MAX_RX_SEQ_IDS 256`, `network_player.h:13`; check in `packet.c` packet_receive).

#### 2.9 Discord

- VERIFIED: optional (`DISCORD_SDK ?= 1`, Makefile line 51; forced off for some targets). Code: `src/pc/discord/discord.c`, `discord_activity.c`, `discord_game_sdk.h`. A fixed application id `APPLICATION_ID_COOPDX` is compiled in (`discord.c:15`).
- VERIFIED features: rich presence and join/invite through activity events; `discord_activity_update` is called after lobby join and after `network_init` (`coopnet.c`, `network.c`). Local Discord id is sent as part of player connect (`get_local_discord_id`, `network_player_connected(... discordId)`).
- CLI `--no-discord` disables it (`cliopts.c:34`).
- Shipped libs: `lib/discordsdk/` and `lib/discord/libdiscord-rpc.*` (binary only).

#### 2.10 Other features seen

- In-game DJUI menu system (own UI toolkit, `docs/c/sections/djui.md`, 120 lines), translations in `lang/`, mod menu hooks, auto-updater (`updater/`, `UPDATER ?= 1`), crash handler (`src/pc/crash_handler.h`), `--console` log, headless mode, save slot chosen by host (`configHostSaveSlot`, `packet_save_file.c`).
- VERIFIED: the host's save file is the shared save. The join packet carries the host's 512-byte EEPROM image (`packet_join.c`), and `PACKET_SAVE_SET_FLAG`/`REMOVE_FLAG` update it. Clients load that image with `save_file_load_all` and a fake level init. So progress belongs to the host.
- Mid-game rejoin/rehost: `network_reconnect_begin`, `network_rehost_begin` (`network.c`).

---

### 3. Network model

#### 3.1 Topology and authority

VERIFIED facts:
- One process is "server" (`gNetworkType == NT_SERVER`), the others are `NT_CLIENT` (`enum NetworkType` also has `NT_NONE`).
- Direct socket mode is a star. `network_send` (`network.c:368`): a client sets `requestBroadcast` and sends only to the server when the system says `requireServerBroadcast`. The server (`packets/packet.c` packet_receive) then re-sends the packet to every other connected player (`packet_duplicate` plus `network_send_to`). Packets with a specific `destGlobalId` (not broadcast, not 0) are forwarded to that one player.
- CoopNet mode sends to each peer directly (`requireServerBroadcast = false`), so the same logical packet reaches everyone without the host's forwarding.
- Level state authority is NOT purely server-side. The host owns the save, the server settings, the mod list, and the moderation decisions. Everything in the game world is owned by whoever is closest (3.3).

#### 3.2 What is synced

VERIFIED by packet list, `packets/packet.h` enum PacketType:

| Group | Packet types | Notes |
| --- | --- | --- |
| Reliability | PACKET_ACK | 1 byte type, 2 byte seq id in header, see 3.5 |
| Player state | PACKET_PLAYER, PACKET_PLAYER_SETTINGS, PACKET_DEATH, PACKET_NETWORK_PLAYERS (+ _REQUEST) | Whole Mario state each send (struct PacketPlayerData, about 150 bytes plus the object's raw regular fields) |
| Objects | PACKET_OBJECT, PACKET_SPAWN_OBJECTS, PACKET_SPAWN_STAR, PACKET_SPAWN_STAR_NLE, PACKET_COLLECT_STAR, PACKET_COLLECT_COIN, PACKET_COLLECT_ITEM | Per sync object; spawn and collect events are reliable |
| World events | PACKET_CHANGE_WATER_LEVEL, PACKET_GLOBAL_POPUP, PACKET_SAVE_FILE, PACKET_SAVE_SET_FLAG, PACKET_SAVE_REMOVE_FLAG | Water level and save flags |
| Level and area sync | PACKET_CHANGE_LEVEL, PACKET_CHANGE_AREA, PACKET_LEVEL_AREA_REQUEST, PACKET_LEVEL_REQUEST, PACKET_LEVEL, PACKET_AREA_REQUEST, PACKET_AREA, PACKET_SYNC_VALID, PACKET_LEVEL_SPAWN_INFO, PACKET_LEVEL_MACRO, PACKET_LEVEL_AREA_INFORM, PACKET_LEVEL_RESPAWN_INFO, PACKET_REQUEST_FAILED | See 3.7 |
| Session | PACKET_JOIN_REQUEST, PACKET_JOIN, PACKET_KICK, PACKET_LEAVING, PACKET_KEEP_ALIVE, PACKET_PING, PACKET_PONG, PACKET_CHAT, PACKET_COMMAND, PACKET_MODERATOR | |
| Mods | PACKET_MOD_LIST_REQUEST, PACKET_MOD_LIST, PACKET_MOD_LIST_ENTRY, PACKET_MOD_LIST_FILE, PACKET_MOD_LIST_DONE, PACKET_DOWNLOAD_REQUEST, PACKET_DOWNLOAD | See 4.6 |
| Lua | PACKET_LUA_SYNC_TABLE_REQUEST, PACKET_LUA_SYNC_TABLE, PACKET_LUA_CUSTOM, PACKET_LUA_CUSTOM_BYTESTRING | See 4.4 and 4.5 |
| Debug | PACKET_DEBUG_SYNC | Desync diagnostics (3.8) |
| Other | PACKET_CUSTOM = 255, PACKET_UNUSED_23 | Reserved |

- 54 `case PACKET_*` handlers in `packet.c` packet_process (VERIFIED by count).
- The enum is a single byte on the wire (`packet_init` writes the type as u8). Several slots are legacy (PACKET_UNUSED_23). The enum order is part of the protocol, so adding types in the middle breaks old clients. That is why the version string must match exactly (3.9).

#### 3.3 Object synchronization and ownership (sync_object)

Files: `src/pc/network/sync_object.c/.h`, `packets/packet_object.c`.

VERIFIED design:
- Every synced game object gets a `struct SyncObject` stored in a hash map keyed by a 32-bit sync id (`sSoMap`, `sync_objects_init_system`). The object stores its sync id in the field `oSyncID`.
- Registration: game code (and Lua through `network_init_object`) calls `sync_object_init(o, maxSyncDistance)`. A `COOP_OBJ_FLAG_NON_SYNC` flag opts an object out. Extra fields are added one at a time with `sync_object_init_field(o, field)` (up to `MAX_SYNC_OBJECT_FIELDS 64`, each with a byte size). The standard fields (position, action, sub-action, interact status, held state, yaw, timer, active flags, gfx node flags, intangible timer, ~20 fields) are sent unless the object is flagged otherwise.
- Sync distance values: `SYNC_DISTANCE_INFINITE (0)`, `SYNC_DISTANCE_ONLY_DEATH (-1.0f)` (only the death event is sent), `SYNC_DISTANCE_ONLY_EVENTS (-2.0f)` (reliable events only), or a positive distance in game units (`network.h`).
- Optional per-object callbacks: `ignore_if_true`, `on_received_pre/post`, `on_sent_pre/post`, `override_ownership`, `on_forget`.
- Id ranges (`sync_object.h:SYNC_ID_BLOCK_SIZE 4096`; `sync_object.c`): while a level loads, objects take ids 1 to 4095 in load order, which is the same on all machines because they load the same level data (deterministic). After load, a new object gets an id from the block `(globalIndex + 1) * 4096` of the creating player (`sync_object_get_available_local_id`). A spawn child can take an id next to its parent (`parent id % 10 == 0`, ten slots). So collisions need no negotiation. (VERIFIED code; the determinism reason is INFERRED.)
- Ownership is dynamic and computed locally on each machine every frame (`network_update_objects` sets `so->owned = sync_object_should_own(id)`):
  - the player who holds an object owns it;
  - otherwise the player closest to the object in the same area owns it (loops over players in the local area, `player_distance`);
  - a mod can override (`override_ownership`);
  - in the credits sequence everyone owns everything and no object packets are sent (act 99).
  Only the owner sends `PACKET_OBJECT` for it. Receivers write the received fields straight into their object.
- Update rate (VERIFIED, `network_update_objects`): `updateRate = distance / 1000` seconds, clamped between `minUpdateRate` (default 0.33 s) and an optional `maxUpdateRate`. So distant objects update at most about 3 per second and nearby ones as often as the update loop allows. A held object always uses 0.33 s.
- Reliability per object: unreliable normally; reliable when the object is deactivated (death) or the sync distance is ONLY_EVENTS (`network_send_object`). The last reliable packet of each object is remembered (`lastReliablePacket`) so it can be resent to a late joiner.
- Ordering: each sent object packet carries a 16-bit `txEventId`. The receiver keeps `rxEventId` per sender and drops older ids (wrap aware, half range compare) (`packet_read_object_header`).
- Safety checks on receive (VERIFIED): unknown sync id, null object, behavior id mismatch (with a short whitelist of allowed behavior changes for penguins and coins), extra-field count mismatch, and "a player holding an object cannot be updated by others". A mismatch makes the receiver "forget" the sync object (`sync_object_forget`, freed after `FORGET_TIMEOUT 10` updates).
- Packets for objects that arrive before the local area finished loading are queued and run later (`network_delayed_packet_object_remember/execute`, `gNetworkAreaLoaded`; load threshold `LOADING_LEVEL_THRESHOLD 10` ticks in `network.c`).
- Platform displacement: when a synced platform object moves, riders on it are moved by the same delta (`network_receive_object`).
- Random seed: each sync object has a 16-bit `randomSeed` that is sent so both sides see the same random choice for object behavior (`packet_write_object_header`).

#### 3.4 Players

- VERIFIED: `PACKET_PLAYER` is unreliable and area-scoped (`packet_init(..., false, PLMT_AREA)`). Payload is a packed struct `PacketPlayerData` (`#pragma pack(1)` in `packet_player.c`): raw object fields, controller state, Mario action, flags, positions, velocity, angles, held object sync ids, and more. The sender writes it from its own MarioState; the receiver writes it into the other player's MarioState (`write_packet_data`).
- VERIFIED send rule (`network_update_player`): send when more than 2 ticks passed since the last send, or the action changed, or any button state changed, or the stick moved. At a 30 Hz game tick that is about 10 packets per second at rest, more when the player acts (INFERRED: the exact rest rate from "ticks > 2").
- VERIFIED: the receiver never accepts a packet about its own player (`network_receive_player`), and ignores players whose recorded level/area is not the same as its own (`currPositionValid = false`).
- VERIFIED: lag compensation exists for PvP: `src/pc/network/lag_compensation.c/.h` keeps a 30-entry history of the local MarioState (`MAX_LOCAL_STATE_HISTORY 30`) so an attack from a remote player can be checked against the state they saw. Exposed to Lua.

#### 3.5 Reliable and unreliable delivery

VERIFIED in `packets/packet_reliable.c`, `packet_read_write.c`, `packet.c`:
- Transport is UDP (socket) or the CoopNet peer link. Reliability is built on top, in the game.
- Packet header: type (1 byte), sequence id (2 bytes; 0 means "unreliable"), flags (1 byte), destination global index (1 byte), then, for ordered packets, sender index (1) + group id (2) + ordered seq id (2), then, for area-scoped packets, course (1), act (1), level (2), area (1). Level-scoped packets omit the area.
- The packet body ends with a 32-bit hash (`packet_hash`, `packet_check_hash`). A bad hash drops the packet ("invalid packet hash!" in `network_receive`). So a corrupted or foreign datagram is rejected.
- Whole packet is deflate-compressed with zlib level 9 (`packet_compress`: `compress2(... Z_BEST_COMPRESSION)`, `packet_decompress`: `uncompress`). `PACKET_LENGTH 3000` is the uncompressed buffer size (`packets/packet.h`).
- Reliable = remember in a linked list, resend until ACK (`network_remember_reliable`, `network_update_reliable`, `network_receive_ack`).
  - Base resend rate `RELIABLE_RESEND_RATE 0.07` seconds, growth by attempts, capped at 4 s; at least 1.25 times the peer's ping (capped at 1.25 s).
  - `MAX_RESEND_ATTEMPTS 15`, then "giving up on reliable packet". Mod-list request gets 60 attempts. Mod and download packets get a longer wait (`adjust_max_elapsed`).
  - Duplicate suppression on receive by `(seqId, hash)` in the last 256 entries per player.
  - Sequence ids are 16 bit, skipping 0 (`sNextSeqNum`).
- Ordered packets (`packets/packet_ordered.c`): a group of packets that must be processed in order (used for the level/area state and the mod list). Entry points `packet_ordered_begin()` / `packet_ordered_end()` wrap the sends. The receiver buffers out-of-order packets in a table per sender and group and processes them in `orderedSeqId` order. Timeout `PACKET_ORDERED_TIMEOUT 30`.
- Unreliable: player packets, most object packets, keep-alives, debug sync.
- Level/area filter: a packet can say "only deliver if the receiver is in the same level" (`PLMT_LEVEL`) or "same level and area" (`PLMT_AREA`). The sender skips players whose recorded location differs (`network_send`, `network_send_to`), and the receiver drops mismatches (`packet_process`). Location is checked by course, act, level number and area index.
- Per-player liveness: `NETWORK_PLAYER_TIMEOUT 15` seconds, ping sent after `NETWORK_PLAYER_PING_TIMEOUT 3` seconds of silence (`network_player.h`, `network_player.c:210-251`).

#### 3.6 Tick rate

- VERIFIED: game logic runs at 30 Hz (`src/pc/pc_main.c:91 #define FRAMERATE 30`). Rendering can be higher; the frame loop targets 60 fps for the interface (`pc_main.c:403`) and the renderer interpolates (INFERRED from the "interpolated" HUD function names in the Lua docs).
- Network updates are driven from the game tick: `network_update()` in `network.c` sends player and object updates, then receives, then updates reliable and ordered queues, once per game tick.
- Timing helpers use wall-clock seconds (`clock_elapsed()`) for object update rates and the reliable resend, and tick counts for the area timer (`network_update_area_timer`, `gNetworkAreaTimer`).
- No fixed network tick, no tick numbers on the wire.

#### 3.7 Rollback, lockstep, prediction

- VERIFIED: no rollback and no lockstep code found. No input-delay logic, no frame number in packets.
- The model is "state replication with local simulation": each machine runs the full game logic for its own player and for objects it owns, and copies other players and other-owned objects from packets. Remote players are shown by writing the received state into their MarioState; no extrapolation code was seen in the files read (extrapolation: UNKNOWN).
- Why this works for SM64 (INFERRED): players can collide and interact, but the interaction results are sent as events (attack, interact, collect packets) and the nearest-player owner decides the result.
- The deterministic RNG is not shared as a global seed; instead each sync object carries a seed (above).

#### 3.8 Late join, level changes and players in different levels

Join flow (VERIFIED from the files named):
1. Client connects (socket: knows the address; CoopNet: joins the lobby).
2. Client sends `PACKET_MOD_LIST_REQUEST` (reliable, to the server) with its version string (`packet_mod_list.c` network_send_mod_list_request). Sets `gAllowOrderedPacketClear = 0`.
3. Server sends an ordered group: `PACKET_MOD_LIST` (version, entry count), one `PACKET_MOD_LIST_ENTRY` per mod (name, incompatible list, relative path, size, directory flag, pausable, ignore-script-warnings, file count), one `PACKET_MOD_LIST_FILE` per file (relative path, size, 16-byte MD5), then `PACKET_MOD_LIST_DONE`.
4. Client checks the version again, caps any mod larger than `MAX_MOD_SIZE` (35 MB), looks up each file's MD5 in its local mod cache, and downloads the missing ones (4.6).
5. After the download completes the client sends `PACKET_JOIN_REQUEST` (`packet_download.c`:202): version string (128 bytes), model, palette, name.
6. Server picks a free global index and sends `PACKET_JOIN`: version again, global index, save slot, all server settings, the 512-byte save image. It then sends the network player table (`network_send_network_players`).
7. Client verifies the version string with `memcmp` over 128 bytes. On mismatch it shuts down and shows "Version mismatch... Someone is out of date!" with both versions (`packet_join.c` network_receive_join). Then it activates the mods, starts the Lua state, requests the network players and the Lua sync tables (`PACKET_LUA_SYNC_TABLE_REQUEST`), fires `HOOK_JOINED_GAME`, and changes level to the entry level.

Level and area sync (VERIFIED, `packet_change_level.c`, `packet_level.c`, `packet_level_request.c`, `packet_area.c`, `network.c` network_on_loaded_area):
- Each machine loads and runs its OWN current level locally. There is no shared "current level" for the party. The server tracks every player's `currCourseNum, currActNum, currLevelNum, currAreaIndex` plus validity flags `currLevelSyncValid`, `currAreaSyncValid` in `struct NetworkPlayer`.
- When a player enters a level (after a 10-tick settle, `network_on_loaded_area`) it sends `PACKET_CHANGE_LEVEL` (or `PACKET_CHANGE_AREA` if only the area changed within a valid level) to the server.
- Server logic `player_changed_level`: look for another player already in that exact area (`get_network_player_from_area`) or at least that level (`get_network_player_from_level`).
  - If nobody is there (or it is the credits), tell the player via `PACKET_SYNC_VALID`: the level starts from its fresh load state. The player is first in that level instance and becomes the reference.
  - If someone is there, send `PACKET_LEVEL_AREA_REQUEST` or `PACKET_LEVEL_REQUEST` to that player. That player answers with an ordered group: `PACKET_LEVEL` (level variables such as coin count, PSS slide started, TTC speed), `PACKET_LEVEL_MACRO` (list of deleted macro objects such as collected coins), `PACKET_LEVEL_SPAWN_INFO` (objects spawned during play), and for area requests `PACKET_AREA` (object states for the area) followed by `PACKET_SYNC_VALID`.
  - A failed or impossible request yields `PACKET_REQUEST_FAILED`.
- The new player then applies those packets over its freshly loaded level (for example the TTC speed fix-up in `network_receive_level`).
- Instance key: the level instance is identified by `(course, act, level, area)`. The castle hub has its act forced to 0 so everyone shares the hub (`packet_change_level.c` network_send_change_level). So two players on different stars of the same course are in different instances and do not see each other.
- Result: players may be in different levels or areas at the same time. Their `PACKET_PLAYER` and `PACKET_OBJECT` traffic is filtered by location on both the sender and receiver side. Remote players in another area are not shown and their positions are marked invalid.
- Credits: object syncing is switched off while `gCurrActStarNum == 99`.

Desync handling (VERIFIED):
- There is no automatic resync of the whole world. Recovery is partial: stale or mismatching sync objects are "forgotten" with log lines (`sync id mismatch`, `behavior mismatch`), last reliable packets are replayed to late joiners, and objects continuously re-send state (no state is only sent once, except events).
- `PACKET_DEBUG_SYNC` (`packets/packet_debug_sync.c`) sends the list of all sync ids and behavior ids every 5 seconds in `DEVELOPMENT` builds (`network_update_objects`) so a developer can compare tables. `print_sync_object_table` dumps the table. Skipped when any player's ping is above 250 ms.
- A "debug packet ring" (`gDebugPacketIdBuffer`, 256 entries) remembers recent packet ids for crash reports (`network.c`).
- Crash handler is started in `network_init` (`crash_handler_init`).
- INFERRED: the project relies on "closest owner wins, others copy" to converge, and on fixed object id assignment from the same level data.

#### 3.9 Version handling and compatibility

- VERIFIED: `src/pc/network/version.h` defines `SM64COOPDX_VERSION "v1.5.1"`, an internal `VERSION_NUMBER 42`, `MINOR_VERSION_NUMBER 1`, and `MAX_VERSION_LENGTH 128`. `get_version()` returns the string (adds the region for non-US ROMs, for example "v1.5.1 EU").
- VERIFIED: the exact-match rule. The same 128-byte string is compared with `memcmp` at the mod list (`packet_mod_list.c:137-145`) and at join (`packet_join.c` network_receive_join) and in the lobby browser (`djui_panel_join_lobbies.c:91`). Any difference means refusal. There is no "compatible range".
- VERIFIED: CoopNet has its own protocol version check, error `MERR_COOPNET_VERSION` ("COOPNET_VERSION" popup, `coopnet.c`).
- VERIFIED: game name separates builds: "sm64coopdx", "sm64coopdx-dev", "sm64coopdx-intl" (non-US ROM) (`version.h`). Lobby lists are filtered by it.
- Mod compatibility handshake: the host's mods ARE the client's mods (the client downloads them, so there is nothing to mismatch). The only compatibility metadata is the `-- incompatible:` header, used locally to stop two incompatible mods from being enabled together (`mods_utils.c`, `mods_incompatible_match`; sent in `PACKET_MOD_LIST_ENTRY`).

---

### 4. Mod system

#### 4.1 Lua API surface

- VERIFIED: Lua 5.3.5 (`lib/lua/include/lua.h:18-21`). Source of the binding code: `src/pc/lua/` (about 53,000 lines in `*.c` of that folder, mostly generated).
- VERIFIED: bindings are generated. `autogen/` holds Python scripts (`extract_functions.py`, `extract_structs.py`, `extract_constants.py`, `gen_hooks.py`, `convert_functions.py`, `autogen.sh`, ...). They scan C headers for function declarations, optionally with a doc comment in the form `/* |description|...|descriptionEnd| */` (seen in `sync_object.h`, `network_player.h`, `lag_compensation.h`) and write `smlua_functions_autogen.c`, `smlua_cobject_autogen.c`, `smlua_constants_autogen.c` and `docs/lua/*.md`.
- VERIFIED counts (counted for this study):
  - `smlua_functions_autogen.c`: 1985 functions named `smlua_func_*`. Plus a small hand-written set in `smlua_functions.c` (the "manually written functions" list at the top of `docs/lua/functions.md`: 18 entries such as `define_custom_obj_fields`, `network_init_object`, `network_send_object`, `network_send_to`, `network_send`, `texture_override_set`, `level_script_parse`, `djui_hud_print_text`). Total is about 2,000 callable functions.
  - `docs/lua/functions*.md`: seven files, about 1,987 function headings (1994 `## ` lines minus 7 navigation lines).
  - `docs/lua/structs.md`: 100 struct sections; `docs/lua/constants.md`: 4,816 lines; `docs/lua/globals.md`: gMarioStates, gNetworkPlayers, gActiveMods, gCharacters, gControllers, gTextures, gObjectAnimations, gLakituState, gServerSettings, gNametagsSettings, gHudDisplay, gLevelValues, gBehaviorValues, gPaintingValues, gFirstPersonCamera, and more.
  - Game structs are exposed as "cobjects" with typed field access (`smlua_cobject.c`), so Lua reads and writes C struct fields directly (for example `gMarioStates[0].pos`, an object's `oPosY`).
- Docs: `docs/lua/lua.md` (index), `docs/lua/guides/` (hooks, mario-state, object-lists, lighting-engine, modfs, vs-code-setup), `docs/lua/examples/` (a dozen example mods incl. bytestring packets, custom level, custom player model, audio, texture override, animations).
- Editor support: `docs/lua/guides/vs-code-setup.md` and `autogen/lua_definitions/` (type stubs for the VS Code Lua extension; folder exists, content not read).
- Dev features: live reload (`smlua_live_reload.c`, enabled by `network_allow_mod_dev_mode`), `--console` logging, `--enable-mod` / `--disable-mods` flags, `require` for multi-file mods (`smlua_require.c`).

#### 4.2 Hooks

- VERIFIED: `enum LuaHookedEventType` in `src/pc/lua/smlua_hooks.h` has 69 event types plus `HOOK_MAX` (70 lines counted in the enum).
- The list (from the enum): UPDATE, MARIO_UPDATE, BEFORE_MARIO_UPDATE, ON_SET_MARIO_ACTION, BEFORE_PHYS_STEP, ALLOW_PVP_ATTACK, ON_PVP_ATTACK, ON_PLAYER_CONNECTED, ON_PLAYER_DISCONNECTED, ON_HUD_RENDER, ALLOW_INTERACT, ON_INTERACT, ON_LEVEL_INIT, ON_WARP, ON_SYNC_VALID, ON_OBJECT_UNLOAD, ON_SYNC_OBJECT_UNLOAD, ON_PAUSE_EXIT, GET_STAR_COLLECTION_DIALOG, ON_SET_CAMERA_MODE, ON_OBJECT_RENDER, ON_DEATH, ON_PACKET_RECEIVE, USE_ACT_SELECT, ON_CHANGE_CAMERA_ANGLE, ON_SCREEN_TRANSITION, ALLOW_HAZARD_SURFACE, ON_CHAT_MESSAGE, OBJECT_SET_MODEL, CHARACTER_SOUND, BEFORE_SET_MARIO_ACTION, JOINED_GAME, ON_OBJECT_ANIM_UPDATE, ON_DIALOG, ON_EXIT, DIALOG_SOUND, ON_HUD_RENDER_BEHIND, ON_COLLIDE_LEVEL_BOUNDS, MIRROR_MARIO_RENDER, MARIO_OVERRIDE_PHYS_STEP_DEFACTO_SPEED, ON_OBJECT_LOAD, ON_PLAY_SOUND, ON_SEQ_LOAD, ON_ATTACK_OBJECT, ON_LANGUAGE_CHANGED, ON_MODS_LOADED, ON_NAMETAGS_RENDER, ON_DJUI_THEME_CHANGED, ON_GEO_PROCESS, BEFORE_GEO_PROCESS, ON_GEO_PROCESS_CHILDREN, MARIO_OVERRIDE_GEOMETRY_INPUTS, ON_INTERACTIONS, ALLOW_FORCE_WATER_ACTION, BEFORE_WARP, ON_INSTANT_WARP, MARIO_OVERRIDE_FLOOR_CLASS, ON_ADD_SURFACE, ON_CLEAR_AREAS, ON_PACKET_BYTESTRING_RECEIVE, ON_FIND_WALL_COLLISION, ON_FIND_CEIL, ON_FIND_FLOOR, ON_FIND_WATER_LEVEL, ON_FIND_POISON_GAS_LEVEL, ON_FIND_SURFACE_ON_RAY, ON_DYNOS_PACK_TOGGLED, BEFORE_PLAY_MODE_UPDATE, ON_PLAY_MODE_UPDATE.
- Naming pattern: `ALLOW_*` hooks return a boolean to veto an engine action; `BEFORE_*` runs before engine code; `ON_*` runs after; `MARIO_OVERRIDE_*` replaces a computed value.
- Other hook functions (docs/lua/guides/hooks.md): `hook_event`, `hook_behavior` (replace or add object behaviors, with init and loop functions), `hook_chat_command`, `hook_mario_action` (custom Mario actions), `hook_on_sync_table_change`, and mod menu entries `hook_mod_menu_text/button/checkbox/slider/inputbox`.
- Design note: the hook list grew from gameplay events (interact, warp) to low-level engine events (find floor, add surface, geo process). That is an API that lets a mod change collision and rendering.

#### 4.3 Per-mod environment

- VERIFIED (`smlua.c` around lines 252-282): each mod gets its own global table (`_ENV`) created once, stored in the Lua registry under the mod's relative path, with `__index` pointing to the real globals. Each mod also gets its own `gGlobalSyncTable` and `gPlayerSyncTable` (`smlua_sync_table_init_globals`).
- VERIFIED: five base functions are removed from the table each mod sees: `load`, `loadfile`, `loadstring`, `collectgarbage`, `dofile` (`smlua.c:267-271`).

#### 4.4 Sync tables

Files: `src/pc/lua/smlua_sync_table.c/.h`, `packets/packet_lua_sync_table.c`; docs in `docs/lua/lua.md`.

VERIFIED behavior:
- `gGlobalSyncTable` holds mod-wide shared values. `gPlayerSyncTable[i]` holds per-player values (16 entries, one table per player index, read-only wrapper `_ReadOnlyTable` around the set).
- The docs warn that "the localIndex for each player is different (unfortunately)": the order of `gMarioStates[]`, `gNetworkPlayers[]` and `gPlayerSyncTable[]` differs between machines. `gPlayerSyncTable[]` translates indices automatically (`docs/lua/lua.md` "Important notes on player indices"; the code uses `network_local_index_from_global` in `smlua_sync_table.c`).
- Writes are sent to everyone as a reliable `PACKET_LUA_SYNC_TABLE` packet (one field per packet): a 64-bit sequence number, the mod's remote index, the key path (array of typed values, limit `MAX_UNWOUND_LNT`), and the value. Value types: integer, number, boolean, string, nil (`struct LSTNetworkType`, `packet.h`).
- Conflict rule: last writer wins using the per-key sequence number. A received write is ignored when its seq is not greater than the stored one (log "Received outdated sync table field packet"). A local write bumps the seq by `MAX_PLAYERS + (MAX_PLAYERS - globalIndex)`, so two players writing at the same time get different numbers and the result is the same on all machines (smlua_sync_table.c:~262). (The tie-break reason is INFERRED.)
- No authority check was found: any client can write `gGlobalSyncTable` (INFERRED from `network_send_lua_sync_table` using `network_send` with no host-only test). A mod must check `network_is_server()` itself.
- Nested tables: allowed, created empty and then filled. Assigning a table over an existing table is refused (error strings in `smlua_sync_table_send_field`).
- Late join: the joiner sends `PACKET_LUA_SYNC_TABLE_REQUEST`; the server replies with every field (`smlua_sync_table_send_all`).
- `hook_on_sync_table_change` runs a Lua function when a field changes locally or over the network (`smlua_sync_table_call_hook`).
- Limit `MAX_UNWOUND_SIZE 256` (a path is up to a bounded number of keys).

#### 4.5 Custom packets from mods

- VERIFIED: `network_send(reliable, table)` and `network_send_to(localIndex, reliable, table)` send a Lua table as `PACKET_LUA_CUSTOM`; `network_send_bytestring` sends raw bytes as `PACKET_LUA_CUSTOM_BYTESTRING`. Receive hooks: `HOOK_ON_PACKET_RECEIVE`, `HOOK_ON_PACKET_BYTESTRING_RECEIVE` (`packets/packet_lua_custom.c`).
- VERIFIED: bytestring limit `MAX_BYTESTRING_LENGTH (PACKET_LENGTH - 15)`; lengths 0 and above the limit are rejected on send and on receive.
- A packet includes the mod's index so it only reaches the same mod (`packet_write(&p, &modIndex, ...)`).

#### 4.6 Mod delivery on join (download, cache, hash)

Files: `packets/packet_mod_list.c`, `packets/packet_download.c`, `src/pc/mods/mod_cache.c`, `mods.c`, `mod.c`.

VERIFIED:
- The host's enabled mods are sent as a manifest (3.8, steps 2-3) and then as file data.
- Hash: MD5, 16 bytes per file (`MD5_Init/Update/Final` in `mod_cache_md5`, `mod_cache.c`). The cache is keyed by hash (`mod_cache_get_from_hash`) and saved to disk (`mod_cache_load`, `mod_cache_save`). If the hash is known, the cached file is reused and nothing is downloaded.
- Not cached: mods whose name contains "(wip)" or "[wip]" (case-insensitive) are written as temporary files and not cached (`packet_download.c` should_cache_mod).
- Remote mod files are written under a per-session temp folder: `.tmp/<6 hex digits>` (`mods_generate_remote_base_path`, `TMP_DIRECTORY ".tmp"`, `mods.c:103-130`). The session name is random from `rand()`.
- Chunked download: `CHUNK_SIZE 800` bytes, `OFFSET_COUNT 50` chunks per group, `GROUP_SIZE = 40000` bytes; two groups active at a time and the next starts when one is half done (`packet_download.c` top and `network_update_offset_groups`). Each chunk is a reliable `PACKET_DOWNLOAD` (about 800 payload bytes, inside the 3000-byte packet). A client request lists the offsets it wants (`PACKET_DOWNLOAD_REQUEST`). The client shows progress and an estimated time (hours/minutes/seconds).
- Size limits: `MAX_MOD_SIZE (35 * 1048576)` = 35 MB per mod (`mods/mods.h:9`). A mod above the limit is disabled and not selectable (`mods_size_enforce`, `mods_utils.c:18-26`) and rejected on receive (`packet_mod_list.c:245-250`).
- Field limits (`mods/mod.h`): name 64, incompatible 256, category 64, description 800 characters.
- What files a mod can contain (`mod.c` `mod_load_files`): `.lua`/`.luac` in the root (a single `.lua` file is a mod by itself; a folder needs `main.lua`); `actors/` with `.bin` and `.col`; `data/` with `.bhv`; `textures/` with `.tex`; `levels/` with `.lvl`; `sound/` with `.m64`, `.mp3`, `.aiff`, `.ogg`. Lua files in sub-folders are not run at start; they are reached with `require` (`smlua.c:362-370`).
- Mod header fields read from the top comment lines of `main.lua` (`mod_extract_fields`): `-- name:`, `-- incompatible:`, `-- category:`, `-- description:`, `-- pausable:`, `-- ignore-script-warnings:`.
- Categories: free-text string field `category` (read-only in Lua per `docs/lua/structs.md:1741`). A fixed category list was not found (UNKNOWN). The bundled mods use `incompatible: gamemode` or `romhack` as tags so only one mod of a kind can be on.
- Bundled example mods in `mods/`: arena, day-night-cycle, faster-swimming.lua, hide-and-seek.lua, cheats.lua, personal-starcount-ex.lua, sm74, star-road (the last two are romhack-style levels).
- Local mods are read from `mods/` next to the game or `<save dir>/mods`, or dragged into the window (`docs/lua/lua.md`).

#### 4.7 DynOS

- VERIFIED: "Dynamic Options System". `dynos.mk` header comment names it that. `credits.txt:81`: "DynOS was created by PeachyPeach." and it is used for "custom models, levels, textures, animations, and warps".
- Code: `data/dynos_*.cpp` (C++): bin readers/writers for actors (`dynos_bin_actor.cpp`), geometry (`geo`, `gfx`, `vtx`), collision (`col`), animations, levels (`lvl`, with validation `dynos_bin_lvl_validate.cpp`), textures (`tex`, `texlist`), behaviors (`bhv`), macro objects, trajectories, movtex, rooms, lights, plus managers (`dynos_mgr_actor/anim/bhv/gfx/pack`).
- Formats (VERIFIED from file extensions in the loaders):
  - `.bin` = binary actor/geo + display list package (compressed or not, `DynOS_Bin_IsCompressed`), with `.col` collision files beside it.
  - `.tex` = texture, built from a `.png` (`dynos_bin_tex.cpp:475-500`).
  - `.lvl` = whole custom level, built from level source files (`dynos_bin_lvl.cpp:1119-1266`).
  - `.bhv` = behavior script built from `.c` text (`dynos_bin_behavior.cpp:2673-2712`).
- Pack build step: a pack folder is converted on first load (`DynOS_ShouldGeneratePack...` checks timestamps and extension pairs) into the binary files the engine loads. The Makefile copies the `dynos/` folder (`dynos.mk`); a "pack" is a folder under `dynos/packs` (shipped examples: "Coop Cafe Pipe", "Fixed Smoke").
- A Lua mod ships DynOS files in its own sub-folders (see 4.6) and the files travel in the same download.
- Lesson: the whole custom-asset path is: author in text/PNG, convert to a compact binary the engine can load fast, ship the binaries and a hash.

#### 4.8 Persistent mod storage and file system

- VERIFIED: `docs/lua/guides/modfs.md`: ModFS = per-mod sandboxed file system, max 32 MB (`MOD_FS_MAX_SIZE`), 512 files (`MOD_FS_MAX_FILES`), path length 256 (`MOD_FS_MAX_PATH`), stored as a ZIP-format `.modfs` file in the `sav` folder, path rules (no leading, trailing or doubled `/`, ASCII only, no `*` or `\`, reserved name `properties.json`). Other mods can read a mod's files if the owner allows it.
- VERIFIED: key/value "mod storage" exists (`src/pc/mods/mod_storage.cpp`; contents not read).
- The save file is the game's EEPROM image (512 bytes) per slot (`gOverrideEeprom`, `packet_join.c`).

#### 4.9 Sandboxing and security

VERIFIED sandbox facts:
- Libraries: `luaopen_base` plus `math`, `string`, `table`, `coroutine`, `utf8` only. `debug`, `io`, `os` and `package` are loaded ONLY when the build has `DEVELOPMENT` and `LUA_UNSAFE` both defined (`smlua.c` init, about lines 318-333; Makefile `LUA_UNSAFE ?= 0`, line 28). So release builds have no direct file or process access from Lua.
- Removed base functions per mod: `load`, `loadfile`, `loadstring`, `collectgarbage`, `dofile` (`smlua.c:267-271`).
- Mods only reach files through ModFS (sandboxed) and mod storage.
- `require` is custom (`smlua_require.c`) and works only inside the mod's own files (INFERRED from the per-mod registry lookup in `smlua_load_script`; not read in detail).

Security concerns seen in the code (INFERRED unless marked):
1. Remote code by design: joining a lobby makes the client download and run the host's Lua automatically. In the files read there is no confirmation prompt between "download complete" and activation (`packet_download.c:187-203`: sets `mod->enabled = true` for every remote mod and sends the join request). A UI warning may exist elsewhere (UNKNOWN). The sandbox is the only protection.
2. Binary Lua chunks: scripts are loaded with `luaL_loadbuffer` (`smlua.c:243`) without a text-only mode, and `.luac` is accepted as a mod file extension (`mod.c`, `smlua.c:362`). Lua 5.3 bytecode is not verified by the VM, and crafted bytecode is a known way to escape Lua sandboxes. This is a design risk, not a proven exploit.
3. Path handling of downloaded files: the relative path arrives from the host in `PACKET_MOD_LIST_FILE` and is joined to a temp folder (`mod_file_full_path`: `concat_path` then `normalize_path`). In the code read (`packet_mod_list.c:270-310`, `mods_utils.c:128-132`) there is no explicit rejection of `..` segments. `normalize_path` may handle it (not verified). Mark as UNKNOWN; worth checking if you build a similar downloader.
4. The path-length field is a 16-bit value read directly into a buffer: `packet_read(p, file->relativePath, relativePathLength * sizeof(u8))` with no visible cap in the lines read (`packet_mod_list.c:297-299`). Other fields (name, incompatible) do check their limits (lines 193-217). INFERRED: a hostile host could send a long path. Not verified whether `packet_read` bounds it.
5. Sync tables: any peer can write any key (4.4), so a mod must not trust sync table values for authority.
6. Peers can send arbitrary `PACKET_LUA_CUSTOM` content; mods must validate.
7. Hash is MD5 (collision-prone) and is used only to identify cache entries, not to prove authenticity. The host controls both the file and its hash, so MD5 collisions matter only if a cache entry from one host can be replaced by another host's file with the same hash (INFERRED, low risk).
8. In direct (socket) mode `packet_spoofed` returns false because the host forwards everything (`packet.c` packet_spoofed: `requireServerBroadcast` returns false). So a client can claim a global index that is not its own, and the host forwards it. Chat spoof protection only applies in CoopNet mode (INFERRED from the code path; whether the host rewrites the index in socket mode was not checked).
9. Whole-state trust: a client controls its own Mario state, object state for objects it owns, and its own save-flag packets (`PACKET_SAVE_SET_FLAG`). There is no server-side validation of movement, so speed hacks and teleports are possible (INFERRED; the only server-side cheat check found is the debug-fly kick).
10. Mod issue tracker: not read (UNKNOWN).

---

### 5. History and upstream handling

VERIFIED statements in the repo:
- `README.md:3`: "...The project was started by the Coop Deluxe Team. The purpose is to actively maintain and improve, but also continue sm64ex-coop, created by djoslin0."
- `credits.txt:83`: "sm64coopdx was built on top of sm64ex-coop, which was built on top of sm64ex, which was built on top of the sm64 decomp project."
- `credits.txt` lists "sm64coopdx Developers" (Agent X, AngelicMiracles, FluffaMario, Isaac0-dev, theclashingfritz, PeachyPeach), "sm64ex-coop Developers" (including djoslin0, who also gets credit for the ROM asset extractor), and a long contributor list for each.
- `CHANGES` (352 lines) is the upstream decompilation's "Refresh N" log. It stops at "Refresh 13" (the last item mentions "No more nonmatchings remain..." and "Shindou matching (#1081)"). It was never extended for the fork.
- `src/pc/network/version.h` has `VERSION_OFFSET 37 // difference from old versioning system`. So the version counter was reset or renumbered when the coopdx line began (the old line used plain integers).
- The tree still holds the N64 build pieces: `tools/` (IDO 5.3 recompiler, n64graphics, audiofile, asm_processor), `lib/src/*.c` (libultra stand-ins: `guLookAtRef.c`, `guMtxF2L.c`, `leointerrupt.c`, and so on), `Makefile` options for `VERSION ?= us` with `jp`, `eu`, `sh` variants, `Dockerfile` that still says "see https://github.com/n64decomp/sm64/blob/master/README.md for advanced usage". The repo root holds ROM hash files `sm64.us.sha1`, `sm64.jp.sha1`, `sm64.eu.sha1` and `sm64.sh.sha1`, and 73 files under `src/` contain `ifdef VERSION_SH` or `VERSION_EU` guards (counted with grep).
- ROM handling: the game extracts assets from the player's own ROM at build time (`extract_assets.py`, `assets.json`). weemitt reads the ROM at run time instead, and neither project commits a ROM.

INFERRED about upstream handling:
- Divergence is large. The fork rewrote the main loop (`src/pc/pc_main.c`), the renderer glue, the object system hooks, the UI (DJUI), and added C++ (DynOS) and Lua. The `CHANGES` file stopping at Refresh 13 means upstream decomp changes (which kept coming) are not tracked in it. A one-commit shallow clone shows no merge commits. So: UNKNOWN whether they merge from upstream; the evidence points to "no regular merging". The credits line "built on top of" treats upstream as an ancestor, not as a source to follow.
- They kept `n64decomp` file layout (`src/game`, `src/engine`, `actors`, `levels`, `include`), so porting a specific upstream fix by hand is still possible.
- Work split: game logic files in `src/game`, `src/engine` kept close to decomp style; everything new is in `src/pc/` (network, lua, mods, djui, discord, gfx, fs). INFERRED purpose: keeps the diff against the decomp tree readable.
- Docs: C docs `docs/c/c.md` only covers naming conventions (camelCase variables, snake_case functions, PascalCase structs, `g` prefix for globals, `s` prefix for statics) and three sections (smlua, configfile, djui). They are short. Lua docs are large and generated.

#### Lessons for a project that builds on a port

INFERRED, from the above:

1. Decide early whether you will keep merging from the port. They did not (or could not): the multiplayer needs changes inside gameplay code (hooks in `mario.c`, `interaction.c`, objects), which makes merges conflict.
2. Put new systems in their own directory (`src/pc/network`, `src/pc/lua`) and keep game-file edits small and marked. weemitt already follows this idea with `src/port/`, `gen/` and `bruteforce/`.
3. Network code needs a stable object identity. They solved it with a fixed id block per player plus load-order ids from deterministic level loading. Glover would need the same: object ids from the level's object list order plus per-player id blocks for spawned objects.
4. Keep a version string exact match at every stage and tie lobby listing to it. The cost is that every release splits the player base. They accept that cost.
5. A network layer behind a function table (`struct NetworkSystem`) let them add CoopNet without changing game code. Do the same.
6. Scripting API generated from headers (autogen) keeps docs and bindings in step. The generator takes a function's doc from an optional `|description|` comment in the C header.
7. Keep the exact old API stable: their `NetworkPlayer` struct keeps "legacy fields to allow mods not to fully break (they don't do anything anymore)" (`network_player.h`). A packet enum with unused slots (PACKET_UNUSED_23) is the same habit.

---

### 6. Testing and CI

- VERIFIED: one workflow, `.github/workflows/build-coop.yaml` ("Build coop"). Triggers: manual, push to `dev`, pull request to `dev`. Push builds run only when the commit message contains "[build]"; draft PRs are skipped.
- Jobs: `build-linux` (ubuntu-22.04, installs build-essential, libglew, libsdl2, zlib, libcurl, zip, runs `make -j`), `build-steamos` (same with `HANDHELD=1`), `build-windows` (windows-latest with MSYS2 mingw64/i686, SDL2 and GLEW packages, `make`), `build-macos-arm` (macos-26, builds GLEW 2.2.0 and SDL2 2.30.9 from source with minimum macOS 11, ad-hoc code sign), `build-macos-intel` (macos-15-intel, same).
- Each job: build, compute a hash of the binary with `tools/hash_file.cpp` and print it as a CI notice, zip, upload artifact. The zip contents: `dynos`, `lang`, `mods`, `palettes`, the Discord SDK lib, the game, and `coopdx_updater`.
- VERIFIED: no test step, no unit tests, no headless network tests, no lint step in CI. A `find` for test files finds only example assets (`actors/test_platform`, `docs/lua/examples/audio-test`, `docs/lua/examples/custom-hud-texture/textures/test.*`). There are `.clang-format` and `.clang-tidy` and `format.sh`, `tools/clang-tidy.sh`, but they are not run in CI.
- Headless mode exists (`--headless`) and a Docker recipe says `make -j HEADLESS=1` (`Dockerfile`), and `gfx_dummy.c` is a no-render backend. Headless mode is for dedicated servers; no automated test that this study found uses it.
- Dev aids: `DEVELOPMENT` build option (Makefile line 23) enables the debug-sync broadcast, extra logs and unsafe Lua libs (with `LUA_UNSAFE`), a fake "broken clock" test in `network_update_area_timer` (commented out), `network_send_debug_sync` every 5 s.
- Lesson: they test by playing. weemitt's `tools/regress.py` plus a headless network test would already cover more than sm64coopdx's CI does.

---

### 7. Platforms and build system

- Build system: GNU Make (`Makefile`, `Makefile.split`, `util.mk`, `dynos.mk`). Option flags: `DEBUG`, `DEVELOPMENT`, `LUA_UNSAFE`, `TARGET_RPI`, `TARGET_RK3588`, `OSX_BUILD`, `TARGET_ARCH`/`TARGET_BITS` (native, 32, 64), `TEXTURE_FIX`, `ENHANCE_LEVEL_TEXTURES`, `DISCORD_SDK`, `COOPNET`, `UPDATER`, `DOCKERBUILD`, `OPT_LEVEL`, `PROFILE`, `ASAN`, `HANDHELD`, `WINDOWS_BUILD`, `VERSION` (us, jp, eu, sh), `GRUCODE` (f3dex2e) (Makefile lines 19-198).
- Operating systems (VERIFIED from CI and Makefile): Linux (x86_64 and ARM: Raspberry Pi 2/3 32-bit, RPi3/4 and RK3588 64-bit `aarch64`; `lib/coopnet/linux` ships x86_64, arm and arm64 libs), SteamOS/handheld (`HANDHELD=1`), Windows (32 and 64 bit, via MinGW; `lib/*/win32`, `win64`), macOS (Apple Silicon and Intel; `lib/*/mac_arm`, `mac_intel`).
- Graphics backends (VERIFIED, `src/pc/gfx/`): OpenGL (`gfx_opengl.c`) and Direct3D 11 (`gfx_direct3d11.cpp`, `gfx_dxgi.cpp`) and a dummy backend (`gfx_dummy.c`); windowing through SDL2 (`gfx_sdl.c`). CLI `--backend opengl|directx` (`cliopts.c:39`).
- Languages: C (game, network, Lua glue), C++ (DynOS, Direct3D, mod storage/fs), Python (asset and binding generators in `tools/`, `autogen/`, `extract_assets.py`).
- Dependencies: SDL2, GLEW, zlib (packet compression, VERIFIED `#include <zlib.h>` in `packet.c`), libcurl (update check, from CI package list), Lua 5.3.5 (prebuilt libs), libcoopnet+libjuice (prebuilt), Discord SDK (prebuilt), miniz (in tree).
- Prebuilt third-party binaries are committed to the repo (`lib/` has `.a`, `.dylib`, `.so`, `.dll` files). That is a supply-chain and license issue (section 1.3) and also why the coopnet client's code could not be read in this study.
- Updater: `updater/` has per-OS binaries (`linux`, `mac_arm`, `mac_intel`, `win64`) and the package includes `coopdx_updater`; `--skip-update-check` flag.
- 32-bit note: they support 32-bit Windows builds (`TARGET_BITS 32`), in line with weemitt's 32-bit x86 builds (INFERRED relevance).

---

### 8. What goes wrong (from docs and code)

Docs are thin on this. What the repo itself says or shows:

- VERIFIED (`docs/lua/lua.md`): player index order differs per machine. Using `gMarioStates[1]` as "the other player" is a bug in multiplayer. Use `gPlayerSyncTable` or the network player's `globalIndex`.
- VERIFIED (`docs/lua/lua.md` Tips): "When a function requests a time parameter, it is almost if not always in frames." and "run the game from a console" to see Lua errors (`--console`).
- VERIFIED (`smlua.c` comments, around `smlua_update`): Lua produces so much garbage that incremental collection lags; they run incremental GC and note "the real fix would be to make smlua produce less garbage". The removal of `collectgarbage` from mods is related (mods cannot stall the game with full collections; INFERRED).
- VERIFIED (code, 3.5): reliable packets give up after 15 attempts. A client that never receives a reliable packet is not told; it can end up in a half-synced state (INFERRED).
- VERIFIED (code, 3.3): object desync is logged, not repaired: "sync id mismatch", "behavior mismatch", "mismatching extra fields count" (an extra-field count mismatch happens when two machines run different mod or game code).
- VERIFIED (`packet_join.c`): the version mismatch message shows both version strings and says "Someone is out of date!".
- VERIFIED: the area sync flow is commented out in `network_update` (a block that re-requested level/area state every 10 seconds if `currLevelSyncValid` stayed false is commented out in `network.c`). So a failed sync request is not retried there; `PACKET_REQUEST_FAILED` is the only reply path (INFERRED consequence).
- VERIFIED: server-side and client-side checks on kick: a client refuses a kick not sent by the server player.
- VERIFIED: the "network startup timer" of 5 seconds of game ticks (`gNetworkStartupTimer = 5 * 30`) makes the host ignore packets from unknown players while it starts (`packet_receive`). Without it, stale datagrams from a previous session would kick people.
- VERIFIED: unknown-sender packets are handled with kick. A packet from an unknown address other than join/mod-list/keep-alive types causes `EKT_CLOSE_CONNECTION` (`packet_receive`).
- Cheating: VERIFIED only the debug-fly check. Everything else is client trusted (4.9, item 9). No anti-cheat design document in the repo (UNKNOWN).
- Mod compatibility: VERIFIED `-- incompatible:` header list; mismatch between a mod and the game version is not checked (a mod does not declare a game version; UNKNOWN whether a Lua API version exists beyond the game version string).
- Issue tracker and wiki: not read (UNKNOWN). Only the wiki Home page was fetched; it repeats the README text and links to the Lua docs.

---

