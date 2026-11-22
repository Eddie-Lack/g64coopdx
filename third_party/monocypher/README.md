# Monocypher

The cryptography library that seals the session's messages (src/session/seal.c).

- Source: https://monocypher.org/download/monocypher-4.0.2.tar.gz
- Version: 4.0.2
- SHA-512 of the archive, as published beside it:
  `bf275d4c53ff94af6cdc723a4e002e9f080f4d1436c86c76bb37870b34807f1d7b32331d8ff8a1aeb369e946f3769021e03e63efac25b82efc5abf54dc084714`
- Files taken, unchanged: `src/monocypher.c`, `src/monocypher.h`, `LICENCE.md`
- License: BSD 2-Clause or CC0, the user's choice (`LICENCE.md`). g64coopdx uses it under BSD 2-Clause.

To update: download the new release, check its published checksum, replace the three files, and run `make test`
(the known-answer tests in tests/net/seal_test.c).
