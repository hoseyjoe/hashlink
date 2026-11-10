# System Font Fallback — Phase 1 (Showing Text) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Heaps-on-HashLink apps draw any character their own SDF font lacks from an installed OS font, on demand, in the same SDF style — JoeClash (game + reader) and Simplified first.

**Architecture:** A plain-C library `fontsys` (FreeType compiled in, plus a per-OS "which font has this code point" lookup) is exposed to HashLink as `fontsys.hdll` and a haxelib `hlfontsys`. A one-field hook in Heaps' `h2d.Font` asks a fallback object for missing characters; `hlfontsys`'s `SystemFontFallback` renders them as SDF into its own atlas pages. Heaps' `TileGroup` already splits draw calls per texture (`h2d.impl.BatchDrawState.setTexture`), so `h2d.Text`/`HtmlText`/`TextInput` need no change to draw glyphs from several textures.

**Tech Stack:** C (C99; DirectWrite COM in one C++ file), FreeType 2.13.3, DirectWrite / CoreText / fontconfig / Android NDK font APIs, CMake + CTest, HashLink 2.0 (`HL_PRIM`), Haxe 4.3.7, Heaps (hoseyjoe fork), haxeui-heaps.

**Spec:** `docs/superpowers/specs/2026-11-10-system-font-fallback-design.md`

**Phase 2 (typing text, IME) is a separate plan written after this one lands.**

## Global Constraints

- Platforms: Windows, macOS, Linux, Android, iOS. **Tested on Windows and Android; macOS, Linux, iOS compile-only, untested.**
- Builds: HashLink JIT, HL/C exe (JoeClash `tools/build_exe.ps1`, including `-Static`), HL/C Android (heaps-android, **minSdk 24**).
- Native core is plain C with **no HashLink types**; HashLink bindings live only in `libs/fontsys/fontsys_hl.c`.
- FreeType ≥ 2.11 (`FT_RENDER_MODE_SDF`); vendored **2.13.3**, its sources compiled straight into the `fontsys` target (no separate library). Windows system libs come in via `#pragma comment(lib, …)` so `build_exe.ps1 -Static` needs no edit.
- SDF format matches the apps' atlases: single channel, **0.5 = edge, inside > 0.5**, stored in the **alpha** channel of RGBA pages with RGB = 255, sampled with the base font's `SDFChannel.Alpha`.
- Spread-to-em ratio **0.25** (GameFont: EM 48, spread 12). Fallback glyphs are authored at **em 32, spread 8**.
- Fallback is consulted **after** `font.charset.resolveChar` (Latin layout unchanged; `en`/`fr` `SceneFitTest` results must not move).
- Han forms follow a BCP 47 language tag (`zh-Hans`, `zh-Hant`, `ja`, `ko`); style = weight (100–900), italic, monospace.
- No eviction; a per-atlas page cap. Page = 1024×1024 RGBA (4 MB). JoeClash cap **2 pages**, Simplified cap **8 pages**.
- Nothing changes for a build that installs no fallback; non-`hl` targets unchanged.
- Work happens on a branch named `system-font-fallback` in each repo touched; never on `master`.

## Review Focus

1. **Android app pause/resume (GL context loss):** fallback glyphs must still draw after resume — pages need a `realloc` that re-uploads from the CPU copy. Pinned in Task 9.
2. **haxeui-heaps cloning and resizing a font after fallback glyphs are already in it** (`clone()` then `resizeTo()`): fallback glyphs must scale with the baked ones. Pinned in Task 6.
3. **Mixed Latin + CJK on one line:** the same baseline, no size jump, and draw calls split per texture rather than glyphs dropped. Pinned in Task 10.
4. **Characters nothing covers** (unassigned code points, lone UTF-16 surrogates from supplementary characters such as emoji, which `h2d.Text` passes one unit at a time): a box, no crash, one OS lookup at most, and lone surrogates never reach the OS. Pinned in Task 10.
5. **Atlas full during long typed text:** new glyphs draw as the box, a single log line, and no growth past the cap. Pinned in Task 9.

---

## File Structure

**hashlink-full (this repo)**
- `include/freetype/` — vendored FreeType 2.13.3 subset (`include/`, `src/base`, `truetype`, `sfnt`, `cff`, `psaux`, `psnames`, `pshinter`, `smooth`, `sdf`, `autofit`, `gzip`), plus `README-hashlink.md` listing the version and what was kept.
- `libs/fontsys/fontsys.h` — the public C API (below). No HashLink includes.
- `libs/fontsys/ftmodule.h` — FreeType module list for this build.
- `libs/fontsys/fontsys.c` — context, face cache, SDF rendering, coverage check (FreeType only).
- `libs/fontsys/fontsys_find_win.cpp` — DirectWrite lookup.
- `libs/fontsys/fontsys_find_apple.c` — CoreText lookup (macOS + iOS).
- `libs/fontsys/fontsys_find_linux.c` — fontconfig lookup.
- `libs/fontsys/fontsys_find_android.c` — `AFontMatcher` (API 29+, resolved with `dlsym`) and `/system/etc/fonts.xml` scan (API 24–28).
- `libs/fontsys/fontsys_hl.c` — `HL_PRIM` bindings.
- `libs/fontsys/CMakeLists.txt`, `libs/fontsys/fontsys.vcxproj(.filters)`; edits to `libs/CMakeLists.txt`, `Makefile`, `hl.sln`.
- `libs/fontsys/haxelib.json` (name `hlfontsys`), `libs/fontsys/fontsys/FontSys.hx` (Haxe binding + wrapper), `libs/fontsys/fontsys/heaps/*.hx` (Heaps glue).
- `libs/fontsys/test/fontsys_test.c` (CTest), `libs/fontsys/test/NativeTest.hx` + `native.hxml`, `libs/fontsys/test/HeapsTest.hx` + `heaps.hxml`.

**Heaps fork** (`C:/HaxeToolkit/haxe/lib/heaps/joe`)
- `h2d/Font.hx` — `FontFallback` interface, `fallback` field, `getChar` change, `clone` copy, `canDraw`.
- `hxd/res/BitmapFont.hx` — static fallback registry applied in `toFont`/`toSdfFont`.
- `tests/font/FontFallbackTest.hx`, `tests/font/build.hxml` — new.

**heaps-android** (`C:/HaxeToolkit/haxe/lib/heaps-android/git`) — `tools/build-hashlink-android.bat`, `android-shell/app/src/main/cpp/CMakeLists.txt`, `MainActivity.kt` `getLibraries()`, `thirdparty/README` rule, prebuilt `.so` files.

**EpubReader** (`D:\_github\EpubReader`) — `src/reader/ui/ReaderFonts.hx`.

**JoeClash** (`D:\_github\_JoeClash`) — `src/Main.hx` (`buildFont`), `src/view/ChatToolkit.hx`, `src/FontFallbacks.hx` (new), `tests/TestContent.hx`, `tests/SceneFitTest.hx`, `tests/FallbackParityTest.hx` (new), `tests.parity.hxml` (new), the hxmls that link heaps.

**Simplified** (`D:\_github\Simplified`) — `src/Main.hx`, `heaps-hl.hxml`, `android.hxml`, `install-libs.ps1`.

---

## C API (fixed here; every later task uses these names)

```c
/* libs/fontsys/fontsys.h */
typedef struct fontsys_ctx fontsys_ctx;
typedef struct { int weight; int italic; int monospace; } fontsys_style;   /* weight 100..900 */
typedef struct {
    int width, height;   /* bitmap size in px, spread padding included */
    int left, top;       /* bitmap origin: px right of pen, px above baseline (padding included) */
    float advance;       /* px */
} fontsys_glyph;

fontsys_ctx *fontsys_create(void);
void fontsys_destroy(fontsys_ctx *ctx);
/* 1 found, 0 nothing covers it, <0 error. path is UTF-8. */
int fontsys_find(fontsys_ctx *ctx, unsigned cp, const char *lang, const fontsys_style *style,
                 char *path_out, int path_cap, int *index_out);
/* face id >= 0, or <0 on failure. Cached by (path, index). */
int fontsys_face_open(fontsys_ctx *ctx, const char *path, int index);
int fontsys_face_has(fontsys_ctx *ctx, int face, unsigned cp);          /* 1 / 0 */
/* 1 ok, 0 face lacks cp, -1 buffer too small (out->width/height still set), <0 error.
   buf receives width*height bytes, row-major, 255 = deepest inside, 128 = edge. */
int fontsys_render_sdf(fontsys_ctx *ctx, int face, unsigned cp, int em_px, int spread_px,
                       unsigned char *buf, int buf_cap, fontsys_glyph *out);
/* Face style queries (FreeType style_flags / FT_IS_FIXED_WIDTH): 1 / 0. Used by tests and Android matching. */
int fontsys_face_is_bold(fontsys_ctx *ctx, int face);
int fontsys_face_is_italic(fontsys_ctx *ctx, int face);
int fontsys_face_is_mono(fontsys_ctx *ctx, int face);
/* Number of OS lookup calls made by fontsys_find so far (test hook). */
int fontsys_debug_os_calls(fontsys_ctx *ctx);
```

---

### Task 1: FreeType vendored + `fontsys` core (open, coverage, SDF render) with CTest

**Files:**
- Create: `include/freetype/**`, `include/freetype/README-hashlink.md`, `libs/fontsys/fontsys.h`, `libs/fontsys/ftmodule.h`, `libs/fontsys/fontsys.c`, `libs/fontsys/fontsys_find_stub.c` (returns 0; replaced per OS in Tasks 2–4), `libs/fontsys/CMakeLists.txt`, `libs/fontsys/test/fontsys_test.c`
- Modify: `libs/CMakeLists.txt` (add `option(WITH_FONTSYS "Build fontsys.hdll." ON)` + `add_subdirectory(fontsys)`, same shape as `WITH_FMT`)

**Interfaces:**
- Produces: the C API above, apart from `fontsys_find`, which returns 0 until Tasks 2–4. CMake target `fontsys.hdll` (via `set_as_hdll`) and a static library `fontsys_core` holding everything except `fontsys_hl.c`, which the test executable `fontsys_test` links.

- [ ] **Step 1: Write the failing test** `libs/fontsys/test/fontsys_test.c` — a plain C runner (`CHECK(cond)` macro, nonzero exit on failure), registered with `add_test(NAME fontsys COMMAND fontsys_test)`. The test font path comes from env `FONTSYS_TEST_FONT`, defaulting to `C:/Windows/Fonts/arial.ttf` on Windows, `/System/Library/Fonts/Helvetica.ttc` on Apple, `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` elsewhere. Cases:
  - `face_open` on the test font returns `>= 0`; opening it again returns the **same** id; a nonexistent path returns `< 0`.
  - `face_has(face, 'H') == 1`; `face_has(face, 0x10FFFD) == 0`.
  - `render_sdf(face, 'H', 32, 8, NULL, 0, &g) == -1` and sets `g.width > 16`, `g.height > 16`.
  - With a big enough buffer, `render_sdf(face, 'H', 32, 8, buf, cap, &g) == 1`, `g.advance > 10`, and `g.width >= 2*8 + 10` (spread padding on both sides plus the ink).
  - `face_is_bold`, `face_is_italic`, `face_is_mono` are all 0 for the regular test font.
  - Pixel checks on that `H`: the first-row/first-column corner pixel `< 32` (outside, at least one spread away); the pixel at the middle of the crossbar `> 160` (inside); at least one pixel in `[112,144]` on the row through the crossbar (an edge exists near 128).
  - `render_sdf(face, 0x10FFFD, …) == 0`.
- [ ] **Step 2: Run to verify it fails** — `cmake -S . -B build && cmake --build build --target fontsys_test` → expected: compile/link errors (no `fontsys_*` symbols).
- [ ] **Step 3: Vendor FreeType 2.13.3** — copy the subset listed under File Structure. In `CMakeLists.txt` compile the single-file module sources (`src/base/ftsystem.c ftinit.c ftdebug.c ftbase.c ftbitmap.c ftglyph.c ftmm.c`, `src/truetype/truetype.c`, `src/sfnt/sfnt.c`, `src/cff/cff.c`, `src/psaux/psaux.c`, `src/psnames/psnames.c`, `src/pshinter/pshinter.c`, `src/smooth/smooth.c`, `src/sdf/sdf.c`, `src/autofit/autofit.c`, `src/gzip/ftgzip.c`) with `FT2_BUILD_LIBRARY` and `FT_CONFIG_MODULES_H="ftmodule.h"`. `ftmodule.h` lists the autofit, tt, cff, psaux, psnames, pshinter, sfnt, smooth, sdf and bsdf modules. TTC collections (`msyh.ttc`, `NotoSansCJK*.ttc`) are handled by sfnt.
- [ ] **Step 4: Implement `fontsys.c`**:
  - The context holds the `FT_Library`.
  - Face cache: up to 32 faces keyed by (path, index), opened with `FT_New_Face`; on Windows the path is converted to wide characters and opened through `FT_Open_Face` with a stream if `FT_New_Face` fails on non-ASCII paths.
  - `face_has` → `FT_Get_Char_Index != 0`.
  - `render_sdf`:
    - `FT_Set_Pixel_Sizes(face, 0, em_px)`,
    - `FT_Property_Set(lib, "sdf", "spread", &spread_px)`,
    - `FT_Load_Glyph(FT_LOAD_DEFAULT)` then `FT_Render_Glyph(FT_RENDER_MODE_SDF)`,
    - copy rows honouring `pitch`,
    - `left = bitmap_left`, `top = bitmap_top`, `advance = advance.x / 64.0`.
  - If FreeType's SDF output turns out to be *outside-high* rather than inside-high, invert it here so the API contract (255 = inside) holds. The pixel checks in Step 1 decide this.
- [ ] **Step 5: Run to verify it passes** — `cmake --build build --target fontsys_test && ctest --test-dir build -R fontsys --output-on-failure` → `100% tests passed`.
- [ ] **Step 6: Commit** — `git add include/freetype libs/fontsys libs/CMakeLists.txt && git commit -m "fontsys: vendored FreeType 2.13.3 and SDF glyph rendering"`

### Task 2: Windows lookup (DirectWrite)

**Files:**
- Create: `libs/fontsys/fontsys_find_win.cpp`
- Modify: `libs/fontsys/CMakeLists.txt` (on `WIN32`, use `fontsys_find_win.cpp` instead of the stub), `libs/fontsys/test/fontsys_test.c`

**Interfaces:**
- Consumes: Task 1's context, `fontsys_face_open`, `fontsys_face_has`.
- Produces: a working `fontsys_find` on Windows.

- [ ] **Step 1: Add the failing tests** (guarded `#ifdef _WIN32` only where noted; otherwise OS-neutral, so they also run on macOS and Linux once Tasks 3–4 land):
  - `find(0x4E2D '中', "zh-Hans", regular)` returns 1. The path exists, `face_open` succeeds, and `face_has(0x4E2D) == 1`.
  - `find('中', "ja")` returns 1 with a **different** (path, index) from `zh-Hans`.
  - `find('A', "en", regular)` returns 1 and covers `A`.
  - `find('A', "en", {700,0,0})` returns a face with `fontsys_face_is_bold == 1`.
  - `find('A', "en", {400,1,0})` returns a face with `fontsys_face_is_italic == 1`.
  - `find('A', "en", {400,0,1})` returns a face with `fontsys_face_is_mono == 1`.
  - `find(0x10FFFD, "en", regular)` returns 0. `find(0xD83D, …)` (a lone surrogate) returns 0 **without calling the OS**; the test checks the exported call counter `fontsys_debug_os_calls(ctx)`.
  - `path_cap` too small returns `<0` and never writes past `path_cap`.
- [ ] **Step 2: Run to verify they fail** — `ctest --test-dir build -R fontsys --output-on-failure` → the find cases FAIL (the stub returns 0).
- [ ] **Step 3: Implement `fontsys_find` (Windows)** in `fontsys_find_win.cpp` (`extern "C"`, `#pragma comment(lib, "dwrite.lib")`):
  - Create `IDWriteFactory2` once per context.
  - `GetSystemFontFallback` → `MapCharacters` over a one-code-point `IDWriteTextAnalysisSource` that reports `lang` as its locale, with the style's weight, style and stretch. For monospace, first try the family `"Consolas"` and keep it if it covers the code point.
  - Take the mapped font → `CreateFontFace` → `GetFiles` → `IDWriteLocalFontFileLoader::GetFilePathFromKey` → UTF-8 path, and `GetIndex` → index.
  - Return 0 for code points in 0xD800–0xDFFF, or above 0x10FFFF, before any OS call.
- [ ] **Step 4: Run to verify they pass** — `ctest --test-dir build -R fontsys --output-on-failure` → `100% tests passed`.
- [ ] **Step 5: Commit** — `git commit -am "fontsys: DirectWrite font lookup"` (`git add` the new file first).

### Task 3: Apple lookup (CoreText) — compile-only

**Files:** Create `libs/fontsys/fontsys_find_apple.c`; modify `libs/fontsys/CMakeLists.txt` (on `APPLE`, link `-framework CoreText -framework CoreFoundation`).

**Interfaces:** Consumes Task 1. Produces `fontsys_find` on macOS/iOS.

- [ ] **Step 1: Implement:**
  - Build a `CTFontDescriptor` with the style's traits (`kCTFontWeightTrait` mapped from 100–900; `kCTFontItalicTrait`; `kCTFontMonoSpaceTrait`) and a base font of `CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0, lang)`.
  - `CTFontCreateForString` over a one-code-point `CFString` (surrogate pair for code points above 0xFFFF; lone surrogates and code points above 0x10FFFF rejected first).
  - Return 0 if the returned font doesn't actually cover the code point (`CTFontGetGlyphsForCharacters`).
  - `CTFontCopyAttribute(kCTFontURLAttribute)` → POSIX path. The index comes from matching the PostScript name against each face in the file (open with FreeType, compare `FT_Get_Postscript_Name`).
- [ ] **Step 2: Verify it compiles:**
  - **macOS:** push the branch; the hashlink CI `darwin` jobs (`.github/workflows/build.yml`) build `fontsys.hdll`. Expected: build green. The CTest results there are informational, not a gate.
  - **iOS:** on a Mac, `cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DBUILD_SHARED_LIBS=OFF -DWITH_VM=OFF -DWITH_SDL=OFF -DWITH_OPENAL=OFF -DWITH_UV=OFF -DWITH_SSL=OFF -DWITH_SQLITE=OFF -DWITH_UI=OFF -DWITH_HEAPS=OFF -DWITH_FMT=OFF -DWITH_FONTSYS=ON && cmake --build build-ios --target fontsys.hdll -- -sdk iphoneos CODE_SIGNING_ALLOWED=NO`. Expected: `** BUILD SUCCEEDED **`. Whether iOS lets the app read system font files is untested in this phase.
- [ ] **Step 3: Commit** — `git add libs/fontsys/fontsys_find_apple.c && git commit -am "fontsys: CoreText font lookup (compile-only)"`

### Task 4: Linux lookup (fontconfig) — compile-only

**Files:** Create `libs/fontsys/fontsys_find_linux.c`; modify `libs/fontsys/CMakeLists.txt` (`find_package(Fontconfig REQUIRED)` on Linux, not Android).

**Interfaces:** Consumes Task 1. Produces `fontsys_find` on Linux.

- [ ] **Step 1: Implement:**
  - Pattern with `FC_CHARSET` holding the code point, `FC_LANG` = lang, `FC_WEIGHT` via `FcWeightFromOpenType`, `FC_SLANT` (italic → `FC_SLANT_ITALIC`), `FC_SPACING` (mono → `FC_MONO`).
  - `FcConfigSubstitute` + `FcDefaultSubstitute`, then `FcFontSort(trim=FcTrue)`.
  - Take the first font whose `FC_CHARSET` contains the code point → `FC_FILE` + `FC_INDEX`.
  - Same surrogate/range guard as Task 2.
- [ ] **Step 2: Verify it compiles** — hashlink CI `linux` jobs green (`apt` installs `libfontconfig1-dev`; add it to the workflow's dependency step). The CTest run there is informational.
- [ ] **Step 3: Commit** — `git add libs/fontsys/fontsys_find_linux.c .github/workflows/build.yml && git commit -am "fontsys: fontconfig font lookup (compile-only)"`

### Task 5: Android lookup, tested on a device

**Files:** Create `libs/fontsys/fontsys_find_android.c`; modify `libs/fontsys/CMakeLists.txt` (on `ANDROID`, link `android` and `log`; build `fontsys_test` too).

**Interfaces:** Consumes Task 1. Produces `fontsys_find` on Android.

- [ ] **Step 1: Implement:**
  - **API 29+:** `dlopen("libandroid.so")` and `dlsym` `AFontMatcher_create`, `AFontMatcher_setStyle`, `AFontMatcher_setLocales`, `AFontMatcher_match`, `AFont_getFontFilePath`, `AFont_getCollectionIndex`, `AFont_close` and `AFontMatcher_destroy`. Symbols are looked up at runtime because minSdk is 24. Match with family `"sans-serif"` (`"monospace"` when mono), the UTF-16 text of the code point, and `lang` as the locale list. Check coverage with `fontsys_face_has`.
  - **API 24–28, or when the symbols are missing:** parse `/system/etc/fonts.xml` once per context, collecting each `<font>` file under `/system/fonts/` with its `lang` attribute, weight and style. Pick the first file whose lang matches the tag's primary subtag (falling back to files without a lang), then the closest weight and style, that `face_has` the code point. Use `index` attributes for `.ttc` files.
  - Same surrogate/range guard as Task 2.
- [ ] **Step 2: Run the CTest binary on a device:**
  ```bash
  cmake -S . -B build-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 -DWITH_VM=OFF -DWITH_SDL=OFF -DWITH_OPENAL=OFF -DWITH_UV=OFF -DWITH_SSL=OFF -DWITH_SQLITE=OFF -DWITH_UI=OFF -DWITH_HEAPS=OFF -DWITH_FMT=OFF -DWITH_FONTSYS=ON
  cmake --build build-android --target fontsys_test
  adb push build-android/bin/fontsys_test /data/local/tmp/ && adb shell FONTSYS_TEST_FONT=/system/fonts/Roboto-Regular.ttf /data/local/tmp/fontsys_test
  ```
  Expected: exit 0 on an API 29+ device. If an API 24–28 emulator is available, run it there too, with `FONTSYS_FORCE_FONTS_XML=1` exercising the XML path on any device. Record the device/API in the commit message.
- [ ] **Step 3: Commit** — `git add libs/fontsys/fontsys_find_android.c && git commit -am "fontsys: Android font lookup (AFontMatcher + fonts.xml)"`

### Task 6: Heaps hook — `h2d.Font.fallback` and the `BitmapFont` registry

Repo: Heaps fork (`C:/HaxeToolkit/haxe/lib/heaps/joe`), branch `system-font-fallback`.

**Files:**
- Modify: `h2d/Font.hx`, `hxd/res/BitmapFont.hx`
- Create: `tests/font/FontFallbackTest.hx`, `tests/font/build.hxml` (`-cp tests/font -cp . -main FontFallbackTest -hl bin/font-test.hl` then `--cmd hl bin/font-test.hl`)

**Interfaces:**
- Produces (Haxe):
  - `interface h2d.Font.FontFallback { function resolveGlyph(font : h2d.Font, code : Int) : Null<h2d.Font.FontChar>; }`
  - `h2d.Font.fallback : Null<FontFallback>` (public var, copied by `clone()`)
  - `h2d.Font.canDraw(code : Int) : Bool` — true when the atlas has it, `charset` maps it, or `fallback` resolves it.
  - `hxd.res.BitmapFont.registerFallback(path : String, fb : Null<h2d.Font.FontFallback>) : Void` (static). `toFont()` and `toSdfFont()` set `font.fallback` from it on **every** return, cached fonts included.

- [ ] **Step 1: Write the failing tests** in `FontFallbackTest.hx`. No engine is needed: build fonts with `@:privateAccess new h2d.Font("t", 32)` and tiles with `new h2d.Tile(null, 0, 0, w, h)`. A fake fallback counts calls and returns `new FontChar(new Tile(null,0,0,10,20), 11)` for `0x4E2D` and `null` otherwise. Cases:
  - `getChar(0x4E2D)` returns the fake glyph (width 11), and a second call doesn't reach the fallback again (count stays 1).
  - `getChar(0x10FFFD)` returns `defaultChar`.
  - **Charset first:** with the default `hxd.Charset`, a font holding `"` but not `“` returns the `"` glyph for `0x201C` and never calls the fallback.
  - `\n` returns `nullChar` and never calls the fallback.
  - `clone()` keeps the `fallback` reference.
  - **Review Focus 2:** resolve `0x4E2D` (tile width 10), then `var c = font.clone(); c.resizeTo(64);` → `c.getChar(0x4E2D).t.width == 20` and `.width == 22`.
  - `canDraw(0x4E2D) == true`, `canDraw(0x10FFFD) == false`, `canDraw('"'.code) == true` when baked.
  - With no fallback set, `getChar` behaves exactly as before for all the cases above (the fallback-less control).
- [ ] **Step 2: Run to verify it fails** — `haxe tests/font/build.hxml` → compile error (`fallback`/`FontFallback` unknown).
- [ ] **Step 3: Implement:**
  - In `getChar`, after `charset.resolveChar` returns null, and only if `code` isn't `\r`/`\n` and `fallback != null`: `c = fallback.resolveGlyph(this, code)`, and if non-null `glyphs.set(code, c)`; otherwise fall through to `defaultChar`.
  - `getChar` may stop being `inline` if the extra branch hurts. Keep it `inline` unless the HL build complains.
  - Add `canDraw`.
  - Copy `fallback` in `clone()`.
  - Add the registry in `BitmapFont`, keyed by `entry.path`.
- [ ] **Step 4: Run to verify it passes** — `haxe tests/font/build.hxml` → output ends `FontFallbackTest: all passed`, exit 0.
- [ ] **Step 5: Commit** — `git add h2d/Font.hx hxd/res/BitmapFont.hx tests/font && git commit -m "h2d.Font: fallback hook for missing glyphs, canDraw, BitmapFont fallback registry"`

### Task 7: HashLink bindings and the `hlfontsys` haxelib

**Files:**
- Create: `libs/fontsys/fontsys_hl.c`, `libs/fontsys/haxelib.json` (`"name": "hlfontsys"`, `"classPath": "."`), `libs/fontsys/fontsys/FontSys.hx`, `libs/fontsys/test/NativeTest.hx`, `libs/fontsys/test/native.hxml`
- Modify: `libs/fontsys/CMakeLists.txt` (add `fontsys_hl.c` to `fontsys.hdll`, link `libhl`)

**Interfaces:**
- Consumes: the C API (Tasks 1–5).
- Produces (Haxe, `package fontsys`):
  - `class FontStyle { var weight:Int; var italic:Bool; var mono:Bool; new(weight=400, italic=false, mono=false); static var REGULAR; static var BOLD; static var ITALIC; static var BOLD_ITALIC; static var MONO; }`
  - `class SdfGlyph { var width:Int; var height:Int; var left:Int; var top:Int; var advance:Float; var pixels:haxe.io.Bytes; }`
  - `class FontSys { new(); function find(code:Int, lang:String, style:FontStyle):Null<{path:String, index:Int}>; function openFace(path:String, index:Int):Int; function faceHas(face:Int, code:Int):Bool; function renderSdf(face:Int, code:Int, em:Int, spread:Int):Null<SdfGlyph>; function dispose():Void; }` — `renderSdf` handles the `-1` retry with a larger buffer itself.
  - `fontsys_hl.c` `HL_PRIM`s named `fontsys_create`, `fontsys_find`, `fontsys_face_open`, `fontsys_face_has`, `fontsys_render_sdf` and `fontsys_destroy`, taking `vbyte*` and ints, with the context as an `hl_abstract` finalized by `fontsys_destroy`.

- [ ] **Step 1: Write the failing test** `NativeTest.hx`. It's plain HashLink (no Heaps), run with `-lib hlfontsys` and asserting with `if (!ok) Sys.exit(1)`:
  - `find(0x4E2D, "zh-Hans", REGULAR) != null`.
  - `openFace` of the result `>= 0`.
  - `renderSdf(face, 0x4E2D, 32, 8)` gives `width > 32`, `pixels.length == width*height`, `advance > 20`.
  - `find(0x10FFFD, "en", REGULAR) == null`.
  - A path with non-ASCII characters round-trips (UTF-8). Copy the test font to `%TEMP%\fontsys_测试.ttf` and `openFace` it, expecting `>= 0`.
- [ ] **Step 2: Run to verify it fails** — `haxe libs/fontsys/test/native.hxml` → compile error (`fontsys.FontSys` unknown).
- [ ] **Step 3: Implement** `fontsys_hl.c` and `FontSys.hx` (`@:hlNative("fontsys")` externs, `String` ↔ UTF-8 `hl.Bytes` via `@:privateAccess s.toUtf8()` / `String.fromUTF8`).
- [ ] **Step 4: Run to verify it passes** — build `fontsys.hdll` into the HashLink install the tests use (`cmake --build build --target fontsys.hdll`, then copy it beside `hl.exe`), then run `haxe libs/fontsys/test/native.hxml` → `NativeTest: all passed`, exit 0. Register it in CMake as `add_test(NAME fontsys.hl …)` next to `hello.hl`.
- [ ] **Step 5: Commit** — `git add libs/fontsys && git commit -m "fontsys: HashLink bindings and hlfontsys haxelib"`

### Task 8: Build integration — Makefile, Visual Studio, JoeClash exe, heaps-android

**Files:**
- Modify (hashlink-full): `Makefile` (add `fontsys` to `LIBS`, add a `FONTSYS` object list, and on Linux add `fontsys_LDLIBS = -lfontconfig`), `hl.sln`
- Create (hashlink-full): `libs/fontsys/fontsys.vcxproj(.filters)` (modelled on `libs/fmt/fmt.vcxproj`)
- Modify (heaps-android): `tools/build-hashlink-android.bat` (`-DWITH_FONTSYS=ON`, plus `:copy_first` for `libhlfontsys.so`), `android-shell/app/src/main/cpp/CMakeLists.txt` (an `IMPORTED` `hlfontsys` and adding it to `target_link_libraries(main …)`), `MainActivity.kt` `getLibraries()` (`"hlfontsys"` before `"main"`), the `thirdparty` README rule (from 9 `.so` files to 10), and the rebuilt `thirdparty/<abi>/*.so`

**Interfaces:** Consumes Task 7's `fontsys.hdll`. Produces `fontsys.hdll` in every pipeline.

- [ ] **Step 1: Desktop builds:**
  - `msbuild hl.sln /p:Configuration=Release /p:Platform=x64` → `x64/Release/fontsys.hdll` exists.
  - `make fontsys.hdll` on Linux (CI) → builds.
- [ ] **Step 2: JoeClash static exe** — with a temporary `-lib hlfontsys` and one call `new fontsys.FontSys().find(0x4E2D, "zh-Hans", fontsys.FontStyle.REGULAR)` traced from `Main`, run `pwsh tools/build_exe.ps1 -Static`. Expected: it links without unresolved externals, and `bin/exe/JoeClash.exe` prints a path. Revert the temporary call afterwards. Task 11 adds the real use.
- [ ] **Step 3: Android** — run `tools\build-hashlink-android.bat` (heaps-android), then `haxe build.android.hxml` in JoeClash with the same temporary call. Expected: the APK installs, `adb logcat` shows the traced path, and `libhlfontsys.so` is in the APK (`unzip -l`).
- [ ] **Step 4: Commit** — in hashlink-full: `git commit -am "fontsys: Makefile and Visual Studio project"`. In heaps-android: `git commit -am "Add libhlfontsys.so (system font fallback)"`. The commit message names the ~50 MB history cost (thirdparty README rule).

### Task 9: `FallbackAtlas` — pages, packing, once-per-frame upload, context loss, cap

**Files:** Create `libs/fontsys/fontsys/heaps/FallbackAtlas.hx`, `libs/fontsys/test/HeapsTest.hx`, `libs/fontsys/test/heaps.hxml` (`-lib heaps -lib hlsdl -lib hlfontsys`, `-main HeapsTest`; `HeapsTest extends hxd.App`, runs its cases in `init()` and on later frames, then `Sys.exit(failures == 0 ? 0 : 1)`, the same pattern as JoeClash's `SceneFitTest`).

**Interfaces:**
- Consumes: `fontsys.SdfGlyph` (Task 7).
- Produces: `class fontsys.heaps.FallbackAtlas { new(pageSize:Int = 1024, maxPages:Int = 2); function place(g:SdfGlyph):Null<h2d.Tile>; function flush():Void; var pageCount(default,null):Int; var uploads(default,null):Int; function dispose():Void; }`
  - A tile's `dx`/`dy` are 0; the caller sets them.
  - Pages are `h3d.mat.Texture`s in RGBA format with `filter = Linear`. CPU pixels are kept with RGB = 255 and A = the SDF value.
  - Each page's `realloc` re-uploads its CPU copy.
  - `flush` is registered once with `haxe.MainLoop.add(flush, -1)` (how `hxd.System` schedules `timeoutTick`, `System.hl.hx:550`) and uploads only dirty pages.
  - When full it returns `null` and logs once: `FallbackAtlas: full (N pages)`.

- [ ] **Step 1: Write the failing tests** (cases in `HeapsTest`, using synthetic `SdfGlyph`s filled with a known byte):
  - Placing 50 glyphs of 40×40 in one frame gives `uploads == 0` straight after; one frame later, `uploads == 1`.
  - Tiles don't overlap (pairwise rectangle check, with a 1 px gutter).
  - With `new FallbackAtlas(256, 2)`, glyphs of 100×100 → the 5th glyph lands on page 2 (`pageCount == 2`), and the 9th returns `null` (**Review Focus 5**). The "full" log line appears exactly once over 3 more `place` calls.
  - Reading back a page with `capturePixels()` after a flush: alpha at a placed glyph's centre equals the synthetic byte, and RGB equals 255.
  - **Review Focus 1:** `page.dispose()`, then reading the texture's `t` (which triggers `realloc`), then `capturePixels()` gives the same alpha as before.
- [ ] **Step 2: Run to verify it fails** — `haxe libs/fontsys/test/heaps.hxml && hl bin/fontsys-heaps-test.hl` → compile error (`FallbackAtlas` unknown).
- [ ] **Step 3: Implement** a shelf packer: rows of the tallest glyph so far, 1 px gutter, and a new page when no shelf fits.
- [ ] **Step 4: Run to verify it passes** — the same command → `HeapsTest: all passed`, exit 0.
- [ ] **Step 5: Commit** — `git add libs/fontsys/fontsys/heaps/FallbackAtlas.hx libs/fontsys/test && git commit -m "hlfontsys: FallbackAtlas pages for fallback glyphs"`

### Task 10: `SystemFontFallback` — glyph source, caches, metrics, language

**Files:** Create `libs/fontsys/fontsys/heaps/GlyphSource.hx`, `libs/fontsys/fontsys/heaps/SystemGlyphSource.hx`, `libs/fontsys/fontsys/heaps/SystemFontFallback.hx`; modify `libs/fontsys/test/HeapsTest.hx`, `libs/fontsys/test/heaps.hxml` (`-lib heaps` must be the fork with Task 6).

**Interfaces:**
- Consumes: `h2d.Font.FontFallback` (Task 6), `FontSys`/`FontStyle`/`SdfGlyph` (Task 7), `FallbackAtlas` (Task 9).
- Produces:
  - `interface fontsys.heaps.GlyphSource { function glyph(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph>; }`
  - `class fontsys.heaps.SystemGlyphSource implements GlyphSource { new(?sys:FontSys); var lastFacePath(default,null):Null<String>; }` — `find` → `openFace` (cached by path+index) → `renderSdf`, with a negative cache per (code, lang, style). `lastFacePath` is the file the most recent successful `glyph` came from (a test hook).
  - `class fontsys.heaps.SystemFontFallback implements h2d.Font.FontFallback { static inline var EM = 32; static inline var SPREAD = 8; new(base:h2d.Font, style:FontStyle, atlas:FallbackAtlas, ?source:GlyphSource); var language:String; var source(default,null):GlyphSource; }`. Without a `source` argument it uses a shared `SystemGlyphSource`.
    - `base` is the unresized font (`BitmapFont.toFont()`), which supplies `base.size` and `base.baseLine`.
    - `language` defaults to `"en"`. Changing it affects only new lookups.

- [ ] **Step 1: Write the failing tests** (in `HeapsTest`, using a fake `GlyphSource` that returns a fixed 40×44 glyph with `left = -8`, `top = 30`, `advance = 32` for `0x4E2D` and `null` otherwise, and counts calls). The base font is a `toFont()` stand-in with `size = 48`, `baseLine = 38`.
  - `font.getChar(0x4E2D)` at `font.size == 48` gives tile width `40 * 48/32 = 60`, `dx = -8 * 1.5`, `dy = (38 - 30*1.5)`, and `FontChar.width = 32*1.5`.
  - A font resized to 24 gives exactly half of each of those.
  - The same code twice in the same font → `source` called once. A second font (different size) sharing the fallback → `source` still called once: glyphs are cached by (code, lang, style), and only the scaled `FontChar` is per font.
  - Unknown code `0x10FFFD` → `defaultChar`. Asked 3 times → `source` called once (miss cache).
  - **Review Focus 4:** `getChar(0xD83D)` → `defaultChar`, and `source` is never called.
  - `language = "ja"` then `getChar(0x4E2E)` → the source receives `"ja"`. The already-cached `0x4E2D` isn't asked again.
  - **Review Focus 3**, using the real `SystemGlyphSource` on Windows: an `h2d.Text` with an SDF base font and the fallback, text `"A中B"`. The test builds the base font in code: a 64×64 `h3d.mat.Texture`, `A` and `B` as `FontChar`s on sub-tiles of it, `type = SignedDistanceField(Alpha, 0.5, -1)`, `size = 48`, `baseLine = 38`.
    - After one frame, `@:privateAccess text.glyphs.content.state` has **3** state entries (page / atlas / page textures).
    - `text.textWidth` is greater than the width of `"AB"` alone.
    - `中`'s tile bottom (`dy + height`) is within ±0.2 em of `A`'s.
  - **Review Focus 5**, end to end: an atlas capped at 1 page of 256 px. Resolve 100 distinct CJK code points through the real source. The ones that don't fit return `defaultChar`, there's no exception, and `pageCount == 1`.
- [ ] **Step 2: Run to verify they fail** — `haxe libs/fontsys/test/heaps.hxml && hl bin/fontsys-heaps-test.hl` → compile error (`SystemFontFallback` unknown).
- [ ] **Step 3: Implement.** In `resolveGlyph(font, code)`:
  - reject surrogates and code points above 0x10FFFF,
  - look up or create the cached authoring glyph: `source.glyph(code, language, style, EM, SPREAD)` → `atlas.place`,
  - build `FontChar(tile.sub(...) scaled by k = |font.size| / EM, advance * k)` with `dx = left * k` and `dy = base.baseLine * |font.size| / base.size - top * k`.

  Both the base font and the fallback are authored at em = their pixel size, so em matching is just this scaling. No other normalization.
- [ ] **Step 4: Run to verify they pass** — the same command → `HeapsTest: all passed`, exit 0.
- [ ] **Step 5: Commit** — `git add libs/fontsys && git commit -m "hlfontsys: SystemFontFallback (OS fonts as SDF glyphs on demand)"`

### Task 11: JoeClash — game font fallback, parity test, test policy

Repo: `D:\_github\_JoeClash`, branch `system-font-fallback`.

**Files:**
- Create: `src/FontFallbacks.hx`, `tests/FallbackParityTest.hx`, `tests.parity.hxml`
- Modify: `src/Main.hx` (`buildFont`, and startup before the first `loadFont`), `src/view/ChatToolkit.hx` (`installFont` — nothing, if the registry covers it; verify), `tests/TestContent.hx` (`testFontCoversEveryLocaleGlyph`), `tests/SceneFitTest.hx` (comment about machine-dependent metrics for fallback locales), every heaps-linking hxml that builds for `hl` (`build.hxml`, `build.dev.hxml`, `build.cheat.hxml`, `build.drive.hxml`, `build.exe.hxml`, `buildDirectx.hxml`, `build.android.hxml`, `build.multi.hxml`, `tests.scenes.hxml`, `tools.*.hxml` that link heaps) — add `-lib hlfontsys`

**Interfaces:**
- Consumes: `BitmapFont.registerFallback` (Task 6), `SystemFontFallback`, `FallbackAtlas`, `FontStyle` (Tasks 9–10).
- Produces: `class FontFallbacks { static var atlas(default,null):FallbackAtlas; static function install():Void; static function setLanguage(lang:String):Void; }`, all inside `#if hl` (the class is a no-op elsewhere).
  - `install()` creates `new FallbackAtlas(1024, 2)` and registers a `SystemFontFallback` for `"GameFont.fnt"` (style `REGULAR`). It's idempotent.
  - `setLanguage` is called with `data.Tr`'s current locale at startup and on a locale change.

- [ ] **Step 1: Write the failing parity test** `tests/FallbackParityTest.hx` (an `hxd.App`, built by `tests.parity.hxml`, `-D resourcesPath=res -lib hlfontsys`):
  - Render `H`, `g`, `é` and `,` through `FontSys.renderSdf` from `art-src/font/GameFont.ttf` at `em = 48`, `spread = 12`.
  - Compare each with the baked glyph in `res/GameFont.fnt`/`.png`:
    - `xadvance` within ±1,
    - `xoffset`/`yoffset` within ±1 (convert: `xoffset = left`, `yoffset = baseLine - top`),
    - width/height within ±2,
    - mean absolute difference of the overlapping alpha ≤ 12/255.

  This pins the SDF encoding and metrics against the existing atlas.
- [ ] **Step 2: Run to verify it fails** — `haxe tests.parity.hxml` → compile error until `-lib hlfontsys` resolves, then any metric mismatch shows as a failure.
- [ ] **Step 3: Fix any mismatch in `fontsys.c`/`SystemFontFallback`, never in the test thresholds.** Then implement `FontFallbacks`, call `FontFallbacks.install()` at the top of `Main.loadFont()`, and add `-lib hlfontsys` to the hxmls.
- [ ] **Step 4: Change `testFontCoversEveryLocaleGlyph`.** It keeps failing for characters the game itself draws (all `en`/`fr` text, `†`, the arrows). A locale listed in a new `static final OS_FALLBACK_LOCALES:Array<String> = []` in `TestContent.hx` is skipped, with a printed note naming it. The list is empty today; adding `ja` later is a one-word change.
- [ ] **Step 5: Run everything:**
  - `haxe tests.parity.hxml && hl bin/parity-tests.hl` → passes.
  - `haxe tests.hxml` → passes.
  - `haxe tests.content.hxml` → passes.
  - `haxe tests.scenes.hxml && hl bin/scene-tests.hl` → passes with the **same** per-screen results for `en` and `fr` as on `master`. Diff its output against a run from `master`. Peak texture stays under 50 MB: no fallback page is created in an `en`/`fr` walk, so `FontFallbacks.atlas.pageCount == 0` at the end, asserted there.
- [ ] **Step 6: Manual check (Windows)** — `haxe build.dev.hxml && hl bin/hashlink/game.hl`. Type or paste a player name containing `中文 日本語 Ελληνικά Русский` in the LAN name field, or temporarily set a title string to it. Every glyph shows, nothing is a box, and the baseline matches the Latin text. Record a screenshot in the PR.
- [ ] **Step 7: Commit** — `git add -A src/FontFallbacks.hx tests tests.parity.hxml src/Main.hx *.hxml && git commit -m "Fall back to OS fonts for characters GameFont lacks (hl)"`

### Task 12: EpubReader — fallback for the eight faces, `TextFallback` uses `canDraw`

Repo: `D:\_github\EpubReader`, branch `system-font-fallback`.

**Files:** Modify `src/reader/ui/ReaderFonts.hx` (`get`, `hasGlyphIn`, plus a new `installFallbacks(atlas)`), and the reader's `hl` hxmls (`build.hxml`, `build.android.hxml`: `-lib hlfontsys`). JoeClash: `src/FontFallbacks.hx` calls it.

**Interfaces:**
- Consumes: Tasks 6, 9 and 10. JoeClash's `FontFallbacks.atlas` (Task 11).
- Produces:
  - `ReaderFonts.installFallbacks(atlas:fontsys.heaps.FallbackAtlas):Void` (`#if hl`) registers one `SystemFontFallback` per face, with the style mapped as follows:
    - Regular → `REGULAR`, Bold → `BOLD`, Italic → `ITALIC`, BoldItalic → `BOLD_ITALIC`,
    - Mono → `MONO`, MonoBold → `{700,false,true}`, MonoItalic → `{400,true,true}`, MonoBoldItalic → `{700,true,true}`.
  - `hasGlyphIn(face, code)` returns `get(face,16).canDraw(code)`.

- [ ] **Step 1: Write the failing tests** — EpubReader's own tests run `--interp` with no Heaps, so these live in JoeClash's `tests/FallbackParityTest.hx` (it already links heaps and the reader):
  - After `ReaderFonts.installFallbacks(FontFallbacks.atlas)`, `ReaderFonts.hasGlyph(0x4E2D) == true` and `ReaderFonts.hasGlyph(0x10FFFD) == false`.
  - `TextFallback.apply("a→b", ReaderFonts.hasGlyph) == "a→b"`: the arrow is now drawn, not replaced by `->`.
  - `ReaderFonts.get(Bold, 20).getChar(0x4E2D)` comes from a bold face: right after the call, `(cast (cast ReaderFonts.get(Bold, 20).fallback : SystemFontFallback).source : SystemGlyphSource).lastFacePath` differs from the path recorded the same way for `Regular` on Windows.
  - Calling `ReaderFonts.release()` and then drawing a frame still resolves `0x4E2D` from the same tile: the page survives because the fallback pages aren't `font.tile`.
- [ ] **Step 2: Run to verify they fail** — `haxe tests.parity.hxml && hl bin/parity-tests.hl` → FAIL (`hasGlyph(0x4E2D)` is false).
- [ ] **Step 3: Implement** `installFallbacks` (via `hxd.res.BitmapFont.registerFallback(face, …)`) and the `hasGlyphIn` change. In JoeClash, `FontFallbacks.install()` calls `ReaderFonts.installFallbacks(atlas)`.
- [ ] **Step 4: Run to verify they pass** — the parity suite passes, and `haxe tests.scenes.hxml && hl bin/scene-tests.hl` is unchanged against `master` for `en`/`fr` (the reader walk included).
- [ ] **Step 5: Commit** — EpubReader: `git commit -am "ReaderFonts: OS font fallback for every face; hasGlyph counts fallback"`. JoeClash: `git commit -am "Install reader font fallbacks"`.

### Task 13: Simplified — MainFont fallback (desktop + Android)

Repo: `D:\_github\Simplified`, branch `system-font-fallback`.

**Files:** Modify `src/Main.hx` (next to `SDFFonts.register("MainFont.fnt", …)`), `heaps-hl.hxml`, `heaps-hl-automation.hxml`, `test-heaps-hl.hxml`, `android.hxml` (`-lib hlfontsys`), `install-libs.ps1` (install `hlfontsys` with `haxelib dev hlfontsys <hashlink-full>/libs/fontsys`, the way `hlsdl` is handled).

**Interfaces:** Consumes Tasks 6, 9 and 10. Produces `#if hl` startup code: `new FallbackAtlas(1024, 8)` and `BitmapFont.registerFallback("MainFont.fnt", new SystemFontFallback(base, FontStyle.REGULAR, atlas))`, with `language` taken from the OS locale (`hxd.System.lang` mapped to BCP 47: `zh` + region `CN`/`SG` → `zh-Hans`, `zh` + `TW`/`HK`/`MO` → `zh-Hant`, else the primary subtag).

- [ ] **Step 1: Write the failing test** `tests-heaps/FontFallbackTest.hx` (an `hxd.App` + `heaps-drive`, the same shape as `RightClickMenuTest`, added to `test-heaps-hl.hxml` with `--next`). It builds a HaxeUI `Label` with text `"根因 分析 Причина Αιτία"` and, after one frame, asserts:
  - no character resolved to `defaultChar` (`label`'s `h2d.Text` font `canDraw` is true for each),
  - the label's width is greater than for `"x"`.
- [ ] **Step 2: Run to verify it fails** — `haxe test-heaps-hl.hxml` → `FontFallbackTest` FAILs (`canDraw` is false).
- [ ] **Step 3: Implement** the startup code and the hxml/libs changes.
- [ ] **Step 4: Run to verify it passes** — `haxe test-heaps-hl.hxml` → all heaps tests pass, the new one included.
- [ ] **Step 5: Manual check (Windows + Android):**
  - `haxe heaps-hl.hxml`, run it, and paste `根本原因分析：数据库连接超时` into a new incident's title. It displays fully.
  - `haxe android.hxml`, install, and paste the same text. It displays fully.
  - On Android, background and resume the app (**Review Focus 1**): the text is still there.

  Typing with an IME is Phase 2.
- [ ] **Step 6: Commit** — `git commit -am "Fall back to OS fonts for characters MainFont lacks (hl)"`

---

## Self-review notes (for the executor)

- **Spec deviation, simplification:** the spec planned a multi-page change to `h2d.Text`. Heaps' `TileGroup` already starts a new draw state when a quad's texture changes (`h2d/impl/BatchDrawState.hx:51`), so no `h2d.Text`/`HtmlText`/`TextInput` change is needed. Task 10 pins this with the 3-state assertion.
- **Calls this plan fixes that the spec left to the builder:**
  - authoring em 32 / spread 8 (keeping GameFont's 0.25 ratio),
  - 1024² RGBA pages; caps of 2 (JoeClash) and 8 (Simplified),
  - shelf packing,
  - `MainLoop` flush,
  - a `BitmapFont` registry keyed by resource path,
  - Android API 24–28 via `/system/etc/fonts.xml`.
- **Known limit, not fixed in phase 1:** supplementary-plane characters (emoji, CJK Extension B) arrive in `h2d.Text` as two UTF-16 units and draw as two boxes.
