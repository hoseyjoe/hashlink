# HarfBuzz (vendored for fontsys)

- Version: **15.1.0**, from the official release tarball
  `https://github.com/harfbuzz/harfbuzz/releases/download/15.1.0/harfbuzz-15.1.0.tar.xz`
  (SHA-256 `0fc7c565443a5f4be7da65ac5ee5bd737d9b158dc908708b7689a0a5796b0c8d`).
- License: "Old MIT" (see `COPYING`).
- Used by: `libs/fontsys` (compiled straight into it; no separate library), to join
  emoji sequences (skin tones, ZWJ families, flags, keycaps, tag flags) into the one
  glyph the emoji font has for them (`fontsys_shape_cluster`).

## What was kept

Copied unmodified from the tarball:

- `src/*.cc`, `src/*.hh`, `src/*.h`, `src/OT/`, `src/ms-use/`, without the tests
  (`src/test*.cc`) and fuzzers
- `COPYING`, `README.md`

Everything else (meson/CMake build files, generator scripts, `.rl` sources, `src/rust`,
`src/wasm`, `src/graph`, `test/`, `util/`, `docs/`, `perf/`, `subprojects/`) was left out.

## How it is built

Only the amalgamation `src/harfbuzz.cc` is compiled, with `HAVE_FREETYPE=1` so `hb-ft`
uses the vendored FreeType (`include/freetype`), and `HB_LEAN` (HarfBuzz's own size
switch, `src/hb-config.hh`: no serialization, glyph names, math, vertical, variations;
shaping, GSUB and AAT stay), which takes about 340 KB off `fontsys.hdll`: `libs/fontsys/CMakeLists.txt`,
`Makefile` and `libs/fontsys/fontsys.vcxproj`. MSVC needs `/bigobj`. Elsewhere it is built
`-Os -fno-exceptions -fno-rtti -fvisibility=hidden`, and `fontsys.hdll` links it with
`--exclude-libs,ALL` (not Apple) so no `hb_*` symbol is exported. `hb-features.h` is not
generated; nothing in `src` includes it.

## Updating

Replace `src/` (keeping the same exclusions), `COPYING` and `README.md` from a newer
release tarball, update the version and SHA-256 above, and rebuild `fontsys_test`.
