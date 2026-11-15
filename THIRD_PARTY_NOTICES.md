# Third-party notices

g64coopdx's own code is under the MIT license ([LICENSE](LICENSE)).

The lobby server uses only Go's standard library.

## weemitt

`weemitt/` is a copy of [weemitt](https://github.com/Eddie-Lack/weemitt), the native PC port of Glover, added with
`git subtree`. `weemitt/SOURCE` names the weemitt commit it was last taken from. It is under weemitt's MIT license
([weemitt/LICENSE](weemitt/LICENSE)), and its own [THIRD_PARTY_NOTICES.md](weemitt/THIRD_PARTY_NOTICES.md) lists the
code that weemitt holds from other projects (Dear ImGui, and the game logic from gloveworld). g64coopdx's changes to
the copy are under the same license.

When a library or another project's code is added, list it here with its source, its version and its full license
text. The libraries planned so far are named in
[docs/discovery/repository.md](docs/discovery/repository.md#license).
