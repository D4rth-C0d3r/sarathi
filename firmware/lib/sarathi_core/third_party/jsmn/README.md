# jsmn

Minimal JSON tokenizer in C: no allocation, the caller supplies the token table. Used by
`json_reader.cpp` only, the single place that includes it (with `JSMN_STATIC`,
`JSMN_STRICT` and `JSMN_PARENT_LINKS`).

- **Upstream:** https://github.com/zserge/jsmn
- **Commit:** `25647e692c7906b96ffd2b05ca54c097948e879c` (2021-10-14, after tag v1.1.0)
- **Files:** `jsmn.h` (SHA-256 `c04533e9181e1e33baceb0f55ac449b05145bb936e8c68cc77dfe0d8277514fb`)
  and `LICENSE` (MIT, Copyright (c) 2010 Serge Zaitsev), copied unmodified.

**Don't edit these files.** Wrap them instead. To update, copy a newer commit's files and
update this README.
