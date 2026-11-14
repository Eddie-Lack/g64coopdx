# Questions for the owner

The owner answered the first questions before the discovery and a second round after it. The answers are below,
with where each one shaped the report. The rest are still open; each has a recommendation.

## Decided

| Topic | Decision | Where it shaped the report |
| --- | --- | --- |
| Players | Design for 16; test and promise 8 until the frame limits are measured with 16 gloves | [state-inventory.md](state-inventory.md#the-frames-hard-limits) |
| What a player is | A glove with its own ball | [README.md](README.md#the-ball) |
| Platforms | Windows first, Linux too | [repository.md](repository.md#the-build) |
| Mods in the first version | Lua scripting and custom levels | [mods.md](mods.md) |
| Game changes | Into weemitt, behind a player count that changes nothing at one player | [weemitt.md](weemitt.md#how-g64coopdx-relates-to-weemitt) |
| Levels | Players may be in different levels of the same world; the session is in one world at a time, and a portal to another world opens when every player is in the hub | [README.md](README.md#worlds-levels-and-instances), [netcode.md](netcode.md#instances) |
| Services | Direct connection first (phase 2); the lobby, NAT traversal and relay later (phase 4) | [roadmap.md](roadmap.md) |
| Joining | In the middle of a level, by replaying its inputs; joining at a new instance as the fallback | [netcode.md](netcode.md#instances) |
| Lives | None in co-op | [README.md](README.md#death-and-respawn) |
| Death | Respawn at the last checkpoint the player's own ball passed | [README.md](README.md#death-and-respawn) |
| Exits | Each player exits alone; the others play on | [README.md](README.md#level-exits) |
| Ball pickup | A host option: own ball only, or any ball. Each ball keeps its real owner; a lost ball kills that owner | [README.md](README.md#the-ball) |
| License | MIT for g64coopdx's own code | [repository.md](repository.md#license) |
| Builds | Published | [repository.md](repository.md#what-may-be-distributed) |
| Name | Decided later; `g64coopdx` is the working name | [repository.md](repository.md#candidate-names) |

## Still open

| # | Question | Recommendation | See |
| --- | --- | --- | --- |
| 1 | Who is the copyright holder in the MIT `LICENSE` file? | Your name or a project name, as in your other repositories | [repository.md](repository.md#license) |
| 2 | Should g64coopdx keep weemitt's rule that the game's memory layout in `port_ram` does not change? | Yes. Player 0 keeps the N64 addresses; other players and larger buffers live outside `port_ram` | [README.md](README.md#where-the-new-state-lives) |
| 3 | Is a self-hosted CI runner that holds your ROM acceptable, for the tests that need the ROM? | Optional; without it those tests run locally | [repository.md](repository.md#ci) |
| 4 | 32-bit builds first, 64-bit when the savestates work there? | Yes | [repository.md](repository.md#the-build) |
| 5 | With any-ball pickup, a player may carry another player's ball out through the exit. What does the ball's real owner get? | A new ball at their checkpoint | [README.md](README.md#level-exits) |
| 6 | Does a level count as done for the save when any player exits it, or only when every player who was in it has? | When any player exits it | [README.md](README.md#garibs-and-progress) |
| 7 | Do gloves and balls collide with each other? | No in the first version: the game has no such physics | [README.md](README.md#the-ball) |
| 8 | Garibs within a level: shared by the players in it, or counted per player? | Shared; the "all garibs" bit when one instance's players have them all | [README.md](README.md#garibs-and-progress) |
| 9 | Pause: a local menu while the game goes on, or a vote that stops everyone? | A local menu; the host may stop the game | [netcode.md](netcode.md#frames-and-time) |
| 10 | Is the Original physics mode (whole frames with smooth drawing) acceptable online, with the Smooth mode off? | Yes: the Smooth mode is not exact between Linux and Windows | [netcode.md](netcode.md#frames-and-time) |
| 11 | May a pack hold the XML of an edited shipped level, or only levels built from scratch and diffs? | Levels from scratch as full XML; edits as diffs against the player's own ROM | [mods.md](mods.md#game-data-and-the-authors-work) |
| 12 | The Lua API's first scope | Rules, HUD, puzzle conditions and actions, and spawns through the game's own functions | [mods.md](mods.md#what-the-api-exposes) |
| 13 | Should Glovebox get an "Export level pack" action and put imported models in banks of their own? | Yes, in phase 5 | [mods.md](mods.md#the-level-pack) |
| 14 | Should players' own glove and ball models be allowed (they change only the picture)? | Yes, as packs, after phase 5 | [mods.md](mods.md#player-models) |
| 15 | A custom level from a pack: which world does it belong to, for the one-world rule? | The world of the shipped level whose row it reuses | [mods.md](mods.md#options-cheapest-first-inferred) |
| 16 | A death in a bonus level, with no lives: does it send only that player out? | Yes; the others play on | [README.md](README.md#bosses-bonus-levels-cameos) |
| 17 | While a cameo (scripted scene) runs, are the other players in that level held still and hidden? | Yes | [README.md](README.md#bosses-bonus-levels-cameos) |
