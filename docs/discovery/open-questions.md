# The owner's decisions

These are the owner's decisions for g64coopdx. The owner made the first fifteen (from Players to Builds) directly.
For the rest, from Copyright holder on, the owner accepted the report's recommendation. Each row links to the part
of the report it shapes.

## Decided

| Topic | Decision | Where it shaped the report |
| --- | --- | --- |
| Players | Design for 16; test and promise 8 until the frame limits are measured with 16 gloves | [state-inventory.md](state-inventory.md#the-frames-hard-limits) |
| What a player is | A glove with its own ball | [README.md](README.md#the-ball) |
| Platforms | Windows first, Linux too | [repository.md](repository.md#the-build) |
| Mods in the first version | Lua scripting and custom levels | [mods.md](mods.md) |
| Game changes | In g64coopdx's own copy of weemitt, changed freely; weemitt's fixes taken by hand | [weemitt.md](weemitt.md#how-g64coopdx-relates-to-weemitt) |
| weemitt's rules | g64coopdx does not follow weemitt's rules for an exact copy of the original game: its memory layout rule, weemitt's baseline (g64coopdx keeps its own), its frame-rate check and its emulator sync test. The rules on the ROM, leaked source, the N64 SDK's source and other ports' code stay | [weemitt.md](weemitt.md#where-the-multi-player-change-must-live) |
| Levels | Players may be in different levels of the same world; the session is in one world at a time, and a portal to another world opens when every player is in the hub | [README.md](README.md#worlds-and-instances), [netcode.md](netcode.md#instances) |
| Services | Direct connection first (phase 2); the lobby, NAT traversal and relay later (phase 4) | [roadmap.md](roadmap.md) |
| Joining | In the middle of a level, by replaying its inputs; joining at a new instance as the fallback | [netcode.md](netcode.md#instances) |
| Lives | None in co-op | [README.md](README.md#death-and-respawn) |
| Death | Respawn at the last checkpoint the player's own ball passed | [README.md](README.md#death-and-respawn) |
| Exits | Each player exits alone; the others play on | [README.md](README.md#level-exits) |
| Ball pickup | A host option: own ball only, or any ball. Each ball keeps its real owner; a lost ball kills that owner | [README.md](README.md#the-ball) |
| License | MIT for g64coopdx's own code | [repository.md](repository.md#license) |
| Builds | Published | [repository.md](repository.md#what-may-be-distributed) |
| Copyright holder | "The g64coopdx contributors" in the MIT `LICENSE` file; the owner may put a name there instead | [repository.md](repository.md#license) |
| Memory layout | Not a rule. Player 0 stays at the N64 addresses; other players' state goes on the game's heap in `port_ram`; a global may grow where needed | [README.md](README.md#where-the-new-state-lives) |
| ROM tests in CI | No self-hosted runner for now; the tests that need the ROM run locally | [repository.md](repository.md#ci) |
| Bitness | 64-bit from the start, for Windows and Linux; no 32-bit builds | [repository.md](repository.md#the-build) |
| A ball carried out by another player | With any-ball pickup, the ball's real owner gets a new ball at their checkpoint | [README.md](README.md#level-exits) |
| A level done | A level counts as done for the save when any player exits it | [README.md](README.md#garibs-and-progress) |
| Collisions between players | None in the first version: gloves and balls pass through each other | [README.md](README.md#the-ball) |
| Garibs | Shared by the players in a level; the "all garibs" bit when one instance's players have them all | [README.md](README.md#garibs-and-progress) |
| Pause | A local menu while the game goes on; the host may stop the game | [netcode.md](netcode.md#frames-and-time) |
| Physics mode online | The Original mode (whole frames, smooth drawing); the Smooth mode is off online | [netcode.md](netcode.md#frames-and-time) |
| Edited shipped levels in packs | Levels built from scratch as full XML; edits of shipped levels only as diffs against the player's own ROM | [mods.md](mods.md#game-data-and-the-authors-work) |
| The Lua API's first scope | Rules, HUD, puzzle conditions and actions, and spawns through the game's own functions | [mods.md](mods.md#what-the-api-exposes) |
| Glovebox | An "Export level pack" action, and imported models in banks of their own, in phase 5 | [mods.md](mods.md#the-level-pack) |
| Player models | Allowed, as packs, after phase 5 | [mods.md](mods.md#player-models) |
| A custom level's world | The world of the shipped level whose row it reuses | [mods.md](mods.md#options-cheapest-first-inferred) |
| Death in a bonus level | Sends only that player out; the others play on | [README.md](README.md#bosses-bonus-levels-and-cameos) |
| Cameos | The other players in that level are held still and hidden while a cameo runs | [README.md](README.md#bosses-bonus-levels-and-cameos) |

## Recommended, not yet accepted

| Topic | Recommendation | Where |
| --- | --- | --- |
| Where the services run | The lobby in a container on Fly.io; the relay and STUN from Cloudflare; builds on GitHub Releases; no domain until the name is chosen | [hosting.md](../hosting.md) |

## Still open

The name. The owner picks it later; `g64coopdx` is the working name ([repository.md](repository.md#candidate-names)).
