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

## Monocypher

`third_party/monocypher/` holds [Monocypher](https://monocypher.org) 4.0.2, unchanged: `monocypher.c`,
`monocypher.h` and its `LICENCE.md`. It is the cryptography library that seals the session's messages
(src/session/seal.c). `third_party/monocypher/README.md` names the source and the archive's checksum. Monocypher is
dual-licensed, BSD 2-Clause or CC0; g64coopdx uses it under BSD 2-Clause:

```
Copyright (c) 2017-2023, Loup Vaillant
Copyright (c) 2017-2019, Michael Savage
Copyright (c) 2017-2023, Fabio Scotoni
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Lua

`weemitt/third_party/lua/` holds [Lua](https://www.lua.org) 5.4.8: its core, its auxiliary library and four of its
libraries. It runs the scripts of level packs (weemitt/src/port/g64lua.c). The pack tool, g64pack, holds its core
and its auxiliary library too, to compile a pack's script (tools/g64pack_lua.c). Two lines are changed, in `llimits.h` and
`luaconf.h`; `weemitt/third_party/lua/README.md` names the source, the archive's checksum and the changes. Lua is under the MIT
license:

```
Copyright (C) 1994-2025 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## libgarib

`src/client/fla2.c`, the bounded reader of compressed (FLA2) banks, is ported from Glovebox's
`crates/core/src/fla2.rs`. Glovebox ported that code from [libgarib](https://github.com/naclomi/libgarib) (by
naclomi), at commit `cf879296afc5108867de03facbca428923618f29`. libgarib is under the MIT license, copyright (c) 2021
Naomi Alterman. g64coopdx's changes to the port are under the same license. The text below is libgarib's
`LICENSE.md` at that commit:

```text
# Note

This repository contains descriptions of software interfaces and
implementations of algorithms that were reverse-engineered in a "clean room"
manner from copies of game software legally possessed by the repository author.
The re-interpretations of these original game softwares are available under the
MIT License, detailed below.

# MIT License

Copyright (c) 2021 Naomi Alterman 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
