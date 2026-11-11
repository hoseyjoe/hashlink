# FreeType (vendored for fontsys)

- Version: **2.13.3**, from the official release tarball
  `https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz`
  (SHA-256 `0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289`).
- License: FreeType License (`docs/FTL.TXT`) or GPLv2 (`docs/GPLv2.TXT`), see `LICENSE.TXT`.
- Used by: `libs/fontsys` (compiled straight into it; no separate library).

## What was kept

Copied unmodified from the tarball:

- `include/` (`ft2build.h`, `freetype/`), without `include/dlg` (only used with `FT_DEBUG_LOGGING`)
- `src/base`, `src/truetype`, `src/sfnt`, `src/cff`, `src/psaux`, `src/psnames`,
  `src/pshinter`, `src/smooth`, `src/sdf`, `src/autofit`, `src/gzip`
- `LICENSE.TXT`, `README`, `docs/FTL.TXT`, `docs/GPLv2.TXT`

Everything else (other font drivers and rasterizers, `builds/`, `devel/`, tests, docs, build scripts) was left out.

## How it is built

`libs/fontsys/CMakeLists.txt` compiles the single-file module sources with
`FT2_BUILD_LIBRARY` and `FT_CONFIG_MODULES_H="ftmodule.h"`; the module list is
`libs/fontsys/ftmodule.h` (autofit, truetype, cff, psaux, psnames, pshinter, sfnt,
smooth, sdf, bsdf). `include/freetype/config/ftoption.h` is the stock one.

## Updating

Replace the directories above from a newer release tarball, keep this file, and
check the source list in `libs/fontsys/CMakeLists.txt` still matches.
