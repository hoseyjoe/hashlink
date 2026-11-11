# Colour Emoji Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emoji in Heaps-on-HashLink text draw in colour from the OS emoji font, with sequences (skin tones, ZWJ, flags, tags, keycaps) joined by HarfBuzz into one picture and one caret stop.

**Architecture:** fontsys (C) gains HarfBuzz (cluster → one glyph id) and a FreeType colour renderer (COLRv0; sbix/CBDT via libpng off Windows). fontsys (Haxe) renders colour glyphs once at 64 px into RGBA colour pages and returns `FontChar`s marked colour. Heaps groups emoji sequences into clusters for layout and the caret, asks a new `ClusterFallback` for emoji clusters, and draws colour glyphs on an untinted second layer of `h2d.Text` / `h2d.HtmlText`.

**Tech Stack:** C (fontsys core), C++ (HarfBuzz amalgamation), FreeType 2.13.3, libpng 1.6.26 (vendored), HashLink HL_PRIM bindings, Haxe 4 / Heaps fork, CMake + Makefile + VS project, heaps-android NDK build.

**Spec:** `docs/superpowers/specs/2026-11-11-colour-emoji-design.md` (read it; this plan argues from it).

## Repos and paths

- hashlink-full: `D:/_github/hashlink-full` (branch `master`) — `libs/fontsys`, `include/`.
- Heaps fork: `C:/HaxeToolkit/haxe/lib/heaps/joe` (branch `master`; haxelib dev `heaps`).
- heaps-android: `C:/HaxeToolkit/haxe/lib/heaps-android/git`.
- Simplified: `D:/_github/Simplified` (branch `main`); its Heaps is the git-ignored clone `.haxelib/heaps/git` (cherry-pick Heaps commits into it); `.haxelib/hlfontsys` is a dev link to `libs/fontsys`.
- JoeClash: `D:/_github/_JoeClash` (uses the haxelib dev Heaps; no edits planned).
- Desktop build dir for fontsys: `D:/_github/hashlink-full/build-fontsys` (VS 18 generator). cmake is not on the bash PATH: `"/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"`.
- Android build dir: `D:/_github/hashlink-full/build-android` (has `bin/fontsys_test`).
- A Pixel (Android 17) is attached over adb; in Git Bash prefix device paths with `MSYS_NO_PATHCONV=1`.

## Global Constraints

- No emoji font or image shipped; emoji come from OS fonts only.
- Latin/CJK layout must not move: JoeClash `SceneFitTest` output byte-identical to the pre-change build; Simplified `test-heaps-hl.hxml` passes (except `HiddenPlaceListTest`, which already fails to compile).
- Emoji ignore the text colour and follow its alpha; drop shadows of emoji are silhouettes in the shadow colour, never colour copies.
- Colour glyphs rendered once at **64 px** em; colour pages RGBA 1024², created on first use, capped (default 2 pages).
- HarfBuzz is used only for emoji clusters (and FE0E text clusters' base lookup is unchanged); ordinary text keeps per-character layout.
- Emoji clusters always come from the OS emoji font even when the app font has the base character; all other clusters and bare code points keep "app font first".
- One caret stop per cluster, whether it draws as one picture or as parts.
- COLRv1-only glyphs are not painted (fall back to outline SDF or box).
- JoeClash's static exe (`tools/build_exe.ps1 -Static`) must still link: on Windows fontsys does **not** compile libpng (Segoe UI Emoji is COLR, no PNG needed); off Windows libpng is compiled into fontsys with symbols hidden.
- Plain C public API in `fontsys.h` (no HashLink types), Kha-ready; HL bindings only in `fontsys_hl.c`.
- Tested on Windows and the Pixel; macOS, iOS, Linux compile-only.
- Line endings: keep each file's existing style (Heaps `.hx` files are CRLF). Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Nothing is pushed.

## Review Focus

1. A single emoji wider than a narrow field's `maxWidth` (wordBreak on, e.g. haxeui text field 10 px wide): layout must terminate and keep the cluster whole on its own line — owned by Task 3 (`wrap: lone cluster wider than maxWidth`).
2. Fonts cloned and resized (haxeui clones and `resizeTo`s per text size): a cluster glyph resolved in one size must come out at the right size in a clone resized afterwards — Task 3 (`cluster glyph follows clone + resizeTo`).
3. Text alpha and colour changes after layout (`textColor`, `alpha`, `color.a`): emoji keep their colours and follow alpha — Task 4 (`colour layer follows text alpha, ignores rgb`).
4. A ZWJ sequence the OS cannot join (cluster miss): draws as parts, one caret stop, one backspace removes all of it — Task 3 (`miss draws parts`, `backspace removes whole cluster`).
5. Colour pages full (a chat full of distinct emoji): further emoji fall back to outline SDF or the box, logged once, never crash — Task 5 (`colour atlas cap`). Colour pages share the SDF pages' CPU copy + `realloc` path, so a lost GPU context restores them the same way.

---

### Task 1: Vendor HarfBuzz; build HarfBuzz (all) and libpng (non-Windows) into fontsys

**Files:**
- Create: `include/harfbuzz/` (release source: `src/` incl. `harfbuzz.cc`, `COPYING`, `README.md`), `include/harfbuzz/README-hashlink.md`
- Modify: `libs/fontsys/CMakeLists.txt`, `Makefile` (fontsys section ~lines 124–180), `libs/fontsys/fontsys.vcxproj` (+ `.filters`)
- Test: `libs/fontsys/test/fontsys_test.c`

**Interfaces:**
- Produces: `fontsys_core` compiled with HarfBuzz (`HAVE_FREETYPE`), and off Windows with `FT_CONFIG_OPTION_USE_PNG` + libpng from `include/png` + zlib. `#include <hb.h>` / `<hb-ft.h>` usable from fontsys sources.

- [ ] **Step 1: Download the latest HarfBuzz release tarball** from `https://github.com/harfbuzz/harfbuzz/releases` into a new empty directory under the scratchpad; record version and SHA-256. Copy `src/` (sources and headers; drop `src/test*`, `src/*fuzzer*`, meson files), `COPYING`, `README.md` into `include/harfbuzz/`. Write `include/harfbuzz/README-hashlink.md` in the style of `include/freetype/README-hashlink.md` (version, URL, SHA-256, licence MIT/“Old MIT”, what was kept, how it is built, updating).
- [ ] **Step 2: Write the failing test** in `fontsys_test.c`:

```c
#include <hb.h>
static void test_harfbuzz_linked(void) {
    unsigned major, minor, micro;
    hb_version(&major, &minor, &micro);
    CHECK(major >= 8);
}
```
(call it from `main`).
- [ ] **Step 3: Run it** — build `fontsys_test` (cmake `--build . --config Release --target fontsys_test` in `build-fontsys`). Expected: compile error, `hb.h` not found.
- [ ] **Step 4: Wire the build.**
  - CMake: add `${INCLUDES_BASE_DIR}/harfbuzz/src/harfbuzz.cc` to `fontsys_core`; include dir `${INCLUDES_BASE_DIR}/harfbuzz/src` PUBLIC to fontsys sources/tests; compile definitions for that source `HAVE_FREETYPE=1`, `HB_NO_MT` not set; MSVC: `/bigobj`, silence its warnings on that file only; non-MSVC: `-Os -fno-exceptions -fno-rtti -fvisibility=hidden` on that file. The CMake project already compiles C++ (`fontsys_find_win.cpp`).
  - Non-Windows only (`if(NOT WIN32)`): add the read-side `include/png/*.c` (png, pngerror, pngget, pngmem, pngpread, pngread, pngrio, pngrtran, pngrutil, pngset, pngtrans) to `fontsys_core` with `-fvisibility=hidden`; `target_compile_definitions(fontsys_core PRIVATE FT_CONFIG_OPTION_USE_PNG)` and include dir `include/png`; link zlib (`find_package(ZLIB)` → `ZLIB::ZLIB`; Android: `z`). `-Wl,--exclude-libs,ALL` already hides them in the `.so`.
  - Makefile: add `include/harfbuzz/src/harfbuzz.o` (C++ rule with `-DHAVE_FREETYPE=1 -I include/harfbuzz/src`, plus `-I include/freetype/include`) to the fontsys FreeType archive list; on non-Windows also the png objects, `-DFT_CONFIG_OPTION_USE_PNG`, `-lz` in `fontsys_LDFLAGS`.
  - VS project: add `harfbuzz.cc` (`HAVE_FREETYPE=1`, `/bigobj`, additional include `..\..\include\harfbuzz\src`) to all four configurations; no png.
- [ ] **Step 5: Run the native tests.** Expected: `fontsys_test: all passed` (with the env var from the current tests: `FONTSYS_TEST_BITMAP_EMOJI` unset is fine).
- [ ] **Step 6: Record sizes.** Before Step 4, build `fontsys` (the hdll target) and note `ls -l build-fontsys/bin/fontsys.hdll`; after Step 4, rebuild and note it again; put both in the commit message.
- [ ] **Step 7: Commit**

```bash
git add include/harfbuzz libs/fontsys/CMakeLists.txt Makefile libs/fontsys/fontsys.vcxproj libs/fontsys/fontsys.vcxproj.filters libs/fontsys/test/fontsys_test.c
git commit -m "fontsys: vendor HarfBuzz <ver>; libpng for FreeType colour bitmaps off Windows"
```

### Task 2: Native cluster shaping and colour rendering

**Files:**
- Create: `libs/fontsys/fontsys_color.c` (HarfBuzz + colour render; keeps `fontsys.c` focused on find/SDF)
- Modify: `libs/fontsys/fontsys.h`, `libs/fontsys/fontsys_internal.h` (accessor for a face's `FT_Face` and its cached `hb_font_t*`), `libs/fontsys/fontsys.c` (face slot gains `hb_font_t *hb`, destroyed with the face), CMake/Makefile/vcxproj source lists
- Test: `libs/fontsys/test/fontsys_test.c`

**Interfaces:**
- Consumes: Task 1's build.
- Produces (in `fontsys.h`):

```c
/* 1 when the face has colour glyph data (FT_HAS_COLOR), else 0. */
int fontsys_face_is_color(fontsys_ctx *ctx, int face);
/* Shapes the code points cps[0..n) with HarfBuzz (direction LTR, segment
   properties guessed). 1 when they shape to exactly one glyph that is not
   .notdef (*glyph_out set), 0 otherwise, <0 on error. */
int fontsys_shape_cluster(fontsys_ctx *ctx, int face, const unsigned *cps, int n, unsigned *glyph_out);
/* Renders glyph (a glyph id) in colour with one em = em_px pixels.
   1 ok: buf receives width*height*4 bytes, RGBA, straight (not premultiplied)
   alpha, row-major; 0 the glyph has no colour image (outline only, or COLRv1
   only, or PNG unsupported in this build); -1 buffer too small
   (out->width/height set); <0 error. out->left/top/advance in pixels at em_px. */
int fontsys_render_color(fontsys_ctx *ctx, int face, unsigned glyph, int em_px,
                         unsigned char *buf, int buf_cap, fontsys_glyph *out);
```

- [ ] **Step 1: Write the failing tests** in `fontsys_test.c` (`test_color_emoji`, Windows block uses `C:/Windows/Fonts/seguiemj.ttf`):
  - `fontsys_face_is_color(seguiemj) == 1`, `fontsys_face_is_color(arial) == 0`.
  - shape `{0x1F44D, 0x1F3FD}` → 1, glyph ≠ cmap glyph of 0x1F44D; shape `{0x1F468,0x200D,0x1F469,0x200D,0x1F467}` → 1; shape `{0x31,0xFE0F,0x20E3}` → 1; shape `{0x2764,0xFE0F}` → 1; shape `{0x1F600}` → 1; shape `{0x41, 0x42}` in arial → 0.
  - render_color of the 😀 glyph at 64: returns 1, `width` and `height` in 48..80, some pixel with `abs(r - b) > 60 && a > 200` (colour, not grey); and some pixel with `a` in 1..127 and `max(r,g,b) > a` (`straight alpha, not premultiplied`: premultiplied colour can never exceed its alpha).
  - render_color on arial's glyph for 'A' → 0.
  - If `FONTSYS_TEST_BITMAP_EMOJI_FLAGS` names `NotoColorEmojiFlags.ttf`: shape `{0x1F1EC,0x1F1E7}` → 1; on non-Windows also render_color → 1 with a coloured pixel; on Windows render_color → 0 (no PNG).
- [ ] **Step 2: Run** — Expected: link errors for the three functions.
- [ ] **Step 3: Implement** in `fontsys_color.c`:
  - `hb_font_t` per face created lazily with `hb_ft_font_create_referenced(ft_face)`, freed in face teardown.
  - shape: `hb_buffer_add_codepoints`, `hb_buffer_set_direction(HB_DIRECTION_LTR)`, `hb_buffer_guess_segment_properties`, `hb_shape`; exactly one output glyph with codepoint ≠ 0 → 1.
  - render: if `FT_HAS_FIXED_SIZES(f)` and the face has no colour layers for the glyph: pick the smallest strike with ppem ≥ em_px (else the largest), `FT_Select_Size`, `FT_Load_Glyph(glyph, FT_LOAD_COLOR)`; otherwise `FT_Set_Pixel_Sizes(f, 0, em_px)`, `FT_Load_Glyph(glyph, FT_LOAD_COLOR)` and `FT_Render_Glyph(FT_RENDER_MODE_NORMAL)`. Anything but `FT_PIXEL_MODE_BGRA` → 0. Convert BGRA premultiplied → RGBA straight. For a strike, downscale by `em_px / strike_ppem` with an area (box) filter in premultiplied space before un-premultiplying; scale left/top/advance by the same factor (advance from `slot->advance.x / 64`, or `metrics.horiAdvance / 64`). Restore nothing: `fontsys_render_sdf` already sets its own size.
- [ ] **Step 4: Run** — Expected: `fontsys_test: all passed` with and without `FONTSYS_TEST_BITMAP_EMOJI_FLAGS="<scratch>/NotoColorEmojiFlags.ttf"` (pull it with `MSYS_NO_PATHCONV=1 adb pull /system/fonts/NotoColorEmojiFlags.ttf <scratch>/`).
- [ ] **Step 5: Commit** — `fontsys: shape emoji clusters with HarfBuzz; render colour glyphs (COLRv0, sbix/CBDT)`.

### Task 3: Heaps clusters — layout, cluster lookup, caret

**Files:**
- Modify: `hxd/Charset.hx`, `h2d/Font.hx`, `h2d/Text.hx`, `h2d/HtmlText.hx`, `h2d/TextInput.hx` (all in the Heaps fork)
- Test: `tests/text/EmojiTextTest.hx` (run: `haxe tests/text/build.hxml` from the Heaps root)

**Interfaces:**
- Produces:
  - `hxd.Charset.clusterEnd(s:String, i:Int):Int` — exclusive end of the cluster starting at `i` (`i < s.length`). Grammar (UTS #51 sequences): one code point, then repeatedly: a variation selector U+FE00–FE0F; an emoji modifier U+1F3FB–1F3FF; U+20E3; a tag U+E0020–E007F; ZWJ U+200D followed by one more code point (a trailing ZWJ joins the cluster alone); and if the first code point is a regional indicator U+1F1E6–1F1FF, at most one more regional indicator directly after it. Ideographic variation selectors U+E0100–E01EF also extend.
  - `hxd.Charset.isEmojiCluster(s:String, start:Int, end:Int):Bool` — true when `[start,end)` contains U+FE0F, U+20E3, an emoji modifier, ZWJ, a regional indicator or a tag character.
  - `h2d.Font.FontChar.isColor : Bool` (default false; copied by `clone()`).
  - `interface h2d.Font.ClusterFallback { function resolveCluster(font:Font, cluster:String, emoji:Bool):Null<FontChar>; }` — separate from `FontFallback` so existing implementers (test fakes in Heaps and Simplified) keep compiling; `Font` uses it when `Std.downcast(fallback, ClusterFallback) != null`. `emoji == false` means a text-presentation cluster (ends in U+FE0E): the implementer resolves its base as non-colour.
  - `h2d.Font.getCluster(s:String, start:Int, end:Int):Null<FontChar>` — one code point: `getChar`. Otherwise: emoji cluster → cached `resolveCluster(this, s.substring(start,end), true)` (before `glyphs`/`charset`); a cluster ending in U+FE0E → the app font's glyph for its first code point if it has one (`glyphs`/`charset`), else cached `resolveCluster(..., false)`; any other cluster (selectors, IVS) → `getChar(first code point)`. Returns null on a miss = "draw the parts". Cache: `clusters : Map<String, FontChar>` plus a miss set; `clone()` copies, `resizeTo`/`setOffset` scale/offset cached cluster chars like `glyphs`.
  - Text/HtmlText layout walks clusters: a cluster with a glyph is one glyph (kerning against the previous code point uses the cluster's first code point); a miss lays out its parts (each visible code point via `getChar`, zero-width ones skipped) as one unbreakable unit. Break-char tests use the cluster's first code point.
  - TextInput: every "inside a character" test (`inPair`, `isPairAt`, `snapPos`, `snapPosBack`, `charEnd`, `nextCursor`, `prevCursor`, `snapCursor`, `textPos`, `moveCursorVertically`) uses cluster boundaries: a position is a boundary when walking `clusterEnd` from the start of its line (last `\n` before it, or 0) lands on it.

- [ ] **Step 1: Write the failing tests** (extend `EmojiTextTest.hx`; make `EmojiFallback` also implement `ClusterFallback`: returns a 30-wide `FontChar` with `isColor = true` for `"\u{1F44D}\u{1F3FD}"`, `"\u{1F468}\u{200D}\u{1F469}"`, `"\u{31}\u{FE0F}\u{20E3}"`, `"\u{1F1EC}\u{1F1E7}"`; null for everything else; counts calls in `clusterCalls`. Its `resolveGlyph` must know U+1F469 and U+1F600 (20 wide); `makeFont` gains a 10-wide `"1"`. `S` below is the test font's size):

```haxe
// segmentation
eq("cluster modifier", hxd.Charset.clusterEnd("\u{1F44D}\u{1F3FD}x", 0), 4);
eq("cluster zwj family", hxd.Charset.clusterEnd("\u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467}", 0), 8);
eq("cluster keycap", hxd.Charset.clusterEnd("1\u{FE0F}\u{20E3}2", 0), 3);
eq("cluster flags pair only", hxd.Charset.clusterEnd("\u{1F1EC}\u{1F1E7}\u{1F1EB}\u{1F1F7}", 0), 4);
eq("cluster tag flag", hxd.Charset.clusterEnd("\u{1F3F4}\u{E0067}\u{E0062}\u{E0073}\u{E0063}\u{E0074}\u{E007F}", 0), 14);
eq("cluster latin", hxd.Charset.clusterEnd("ab", 0), 1);
eq("cluster cjk ivs", hxd.Charset.clusterEnd("\u{845B}\u{E0100}x", 0), 3);
eq("emoji cluster keycap", hxd.Charset.isEmojiCluster("1\u{FE0F}\u{20E3}", 0, 3), true);
eq("not emoji cluster ivs", hxd.Charset.isEmojiCluster("\u{845B}\u{E0100}", 0, 3), false);
// layout
eq("joined thumb width", t.calcTextWidth("\u{1F44D}\u{1F3FD}"), 30.);
eq("keycap beats app font 1", t.calcTextWidth("1\u{FE0F}\u{20E3}"), 30.);   // app font: give "1" a 10-wide glyph in makeFont
eq("miss draws parts", t.calcTextWidth("\u{1F469}\u{200D}\u{1F600}"), 40.); // 20 + 20, ZWJ invisible
eq("latin unchanged", t.calcTextWidth("aa a"), 40.);
// wrap
w.maxWidth = 25; eq("wrap: lone cluster wider than maxWidth", w.splitText("\u{1F44D}\u{1F3FD}\u{1F44D}\u{1F3FD}"), "\u{1F44D}\u{1F3FD}\n\u{1F44D}\u{1F3FD}");
eq("wrap: miss parts stay together", w.splitText("a\u{1F469}\u{200D}\u{1F600}"), "a\n\u{1F469}\u{200D}\u{1F600}");
// clone + resize
fb.clusterCalls = 0; font.getCluster("\u{1F44D}\u{1F3FD}", 0, 4); var f3 = font.clone(); f3.resizeTo(S * 2);
eq("cluster glyph follows clone + resizeTo", f3.getCluster("\u{1F44D}\u{1F3FD}", 0, 4).width, 60.);
eq("cluster resolved once", fb.clusterCalls, 1);
// caret
ti.text = "a\u{1F468}\u{200D}\u{1F469}b";
eq("next over family", ti.nextCursor(1), 6); eq("prev over family", ti.prevCursor(6), 1); eq("snap inside family", ti.snapCursor(3), 6);
ti.text = "a\u{1F469}\u{200D}\u{1F600}"; // a miss: drawn as parts, still one cluster
eq("backspace removes whole cluster", ti.prevCursor(6), 1);   // backspace deletes [prevCursor(cursor), cursor)
var p = @:privateAccess ti.textPos(25, 0);                     // x inside the parts (a = 10, woman 10..30)
eq("click lands on cluster boundary", p == 1 || p == 6, true);
// html
h.text = "a\u{1F44D}\u{1F3FD}a"; eq("html joined width", h.textWidth, 50.);
```
Keep every existing assertion in the file passing.
- [ ] **Step 2: Run** `haxe tests/text/build.hxml` — Expected: compile errors (`clusterEnd`, `ClusterFallback`, `getCluster` missing).
- [ ] **Step 3: Implement** `Charset.clusterEnd` / `isEmojiCluster`, `FontChar.isColor`, `ClusterFallback`, `Font.getCluster` (+ clone/resizeTo/setOffset), cluster walking in `Text.splitRawText` / `Text.initGlyphs` / `HtmlText.buildSizes` / `HtmlText.addNode`, and TextInput boundaries as in Interfaces. In this task colour glyphs still go to `glyphs` (Task 4 adds the colour layer).
- [ ] **Step 4: Run** `haxe tests/text/build.hxml`, `haxe tests/font/build.hxml`, `haxe tests/ime/build.hxml` — Expected: `EmojiTextTest: all passed`, `FontFallbackTest: all passed`, `ImeTextTest: all passed`.
- [ ] **Step 5: Commit** — `h2d: emoji clusters (UTS #51) for layout and caret; Font.getCluster + ClusterFallback; FontChar.isColor`.

### Task 4: Heaps colour layer in Text and HtmlText (untinted, alpha-following, silhouette shadow)

**Files:**
- Modify: `h2d/Text.hx`, `h2d/HtmlText.hx`
- Test: `tests/text/EmojiTextTest.hx`

**Interfaces:**
- Consumes: `FontChar.isColor` (Task 3).
- Produces: `h2d.Text.colorGlyphs : Null<h2d.TileGroup>` (private; created on the first colour glyph, child of the Text, `visible = false`, `smooth = true`, cleared with `glyphs` on rebuild). Drawn by `Text.draw` after `glyphs` via `colorGlyphs.drawWith(ctx, colorGlyphs)` after copying the Text's `absX/absY/matA..matD` into it and setting `colorGlyphs.color` to `(1, 1, 1, this.color.a)`. Shadow pass (both classes): before the text, draw `colorGlyphs` offset like the text shadow with an `h3d.shader.FixedColor` (USE_ALPHA = false, rgb = shadow colour) and alpha `dropShadow.alpha * color.a`. HtmlText shares the same single `colorGlyphs` for all its nodes (fonts of `<font>` tags included).

- [ ] **Step 1: Write the failing tests:**

```haxe
t.text = "a\u{1F44D}\u{1F3FD}a"; @:privateAccess t.updateSize();
eq("colour glyph not in text glyphs", t.glyphs.count(), 2);
eq("colour glyph on colour layer", t.colorGlyphs.count(), 1);
t.textColor = 0x000000; t.color.a = 0.5; @:privateAccess t.syncColorLayer();   // the method Text.draw uses to copy transform/alpha
eq("colour layer follows text alpha, ignores rgb", [t.colorGlyphs.color.r, t.colorGlyphs.color.g, t.colorGlyphs.color.b, t.colorGlyphs.color.a].join(","), "1,1,1,0.5");
t.text = "aa"; @:privateAccess t.updateSize();
eq("colour layer cleared on rebuild", t.colorGlyphs.count(), 0);
h.text = "a<font color='#ff0000'>\u{1F44D}\u{1F3FD}</font>a"; @:privateAccess h.updateSize();
eq("html colour glyph on colour layer", h.colorGlyphs.count(), 1);
```
(`syncColorLayer():Void` is a private `Text` method: copies transform and sets the layer colour; `draw` calls it.)
- [ ] **Step 2: Run** — Expected: compile error (`colorGlyphs`).
- [ ] **Step 3: Implement** per Interfaces in `Text` (add/clear/draw/shadow; `initGlyphs` routes `e.isColor` glyphs to `colorGlyphs`) and `HtmlText` (`addNode` routes colour glyphs; `draw` draws the layer and its shadow).
- [ ] **Step 4: Run** the three Heaps test builds — all pass.
- [ ] **Step 5: Commit** — `h2d.Text/HtmlText: colour glyphs on an untinted layer; silhouette drop shadow`.

### Task 5: fontsys Haxe side — HL bindings, colour pages, colour FontChars

**Files:**
- Create: `libs/fontsys/fontsys/ColorGlyph.hx`
- Modify: `libs/fontsys/fontsys_hl.c`, `libs/fontsys/fontsys/FontSys.hx`, `libs/fontsys/fontsys/heaps/GlyphSource.hx`, `libs/fontsys/fontsys/heaps/SystemGlyphSource.hx`, `libs/fontsys/fontsys/heaps/FallbackAtlas.hx`, `libs/fontsys/fontsys/heaps/SystemFontFallback.hx`
- Test: `libs/fontsys/test/HeapsTest.hx` (run from repo root: `haxe libs/fontsys/test/heaps.hxml && C:/SDKs/hashlink/hl.exe bin/fontsys-heaps-test.hl`; copy the freshly built `build-fontsys/bin/fontsys.hdll` to `bin/` and to `C:/SDKs/hashlink/` first, keeping a `.pre-emoji` copy of the installed one)

**Interfaces:**
- Consumes: Task 2 C API; Task 3 `ClusterFallback`, `FontChar.isColor`.
- Produces:
  - HL prims: `hl_face_is_color(ctx, face):bool`, `hl_shape_cluster(ctx, face, cps:bytes /*int32 array*/, n:int, glyph:ref int):int`, `hl_render_color(ctx, face, glyph, em, buf, cap, width, height, left, top:ref int, advance:ref f64):int`.
  - `class fontsys.ColorGlyph { public var width:Int; public var height:Int; public var left:Int; public var top:Int; public var advance:Float; public var pixels:haxe.io.Bytes; /* RGBA straight */ }`
  - `FontSys.faceIsColor(face:Int):Bool`, `FontSys.shapeCluster(face:Int, codes:Array<Int>):Int` (glyph id, 0 = not one glyph), `FontSys.renderColor(face:Int, glyph:Int, em:Int):Null<ColorGlyph>` (grows its buffer on -1 like `renderSdf`).
  - `GlyphSource.colorGlyph(codes:Array<Int>, lang:String, style:FontStyle, em:Int):Null<ColorGlyph>`. `SystemGlyphSource`: find a font for `codes[0]` (find results cached per (code, lang, style) so the SDF and colour paths share one OS call); if that face is not colour and `codes.length > 1`, find for U+1F600 instead (keycaps, ©️, ❤️); not colour → null; `shapeCluster` → 0 → null; `renderColor` at `em`. Misses cached.
  - `FallbackAtlas.new(pageSize = 1024, maxPages = 2, maxColorPages = 2)`, `placeColor(g:ColorGlyph):Null<h2d.Tile>`, `colorPageCount`. Colour pages: RGBA straight, own shelf packer, CPU copy + `realloc` + once-per-frame upload exactly like SDF pages; full → null, logged once (`FallbackAtlas: colour pages full`).
  - `SystemFontFallback implements FontFallback, ClusterFallback`; `public static inline var COLOR_EM = 64;`. `resolveGlyph(font, code)`: colour first (`colorGlyph([code])`), else the existing SDF path. `resolveCluster(font, cluster, emoji)`: emoji → `colorGlyph(codes of cluster)`, else null-colour SDF of the first code point; null → miss. Colour `FontChar`: `isColor = true`, scaled by `font.size / COLOR_EM`, baseline-aligned like SDF glyphs (`dy = base.baseLine * size/|base.size| - top*k`), advance `advance*k`. Glyph cache keyed by cluster text + lang + style.

- [ ] **Step 1: Write the failing tests** in `HeapsTest.hx`:
  - Fake-source atlas tests: `placeColor` of a 64×64 glyph returns a tile on a page different from SDF pages; 3 pages' worth with `maxColorPages = 1` → later placements null, no exception (`colour atlas cap`); `colorPageCount == 1`.
  - Real OS (Windows only): with the existing `textFont` fallback, `textFont.getCluster("\u{1F44D}\u{1F3FD}", 0, 4)` is non-null, `isColor`, its texture is a colour page (not `textFont.tile`'s, not the SDF page of U+4E2D); `textFont.getChar(0x1F600).isColor`; `textFont.getCluster("1\u{FE0F}\u{20E3}", 0, 3).isColor`; `textFont.getChar(0x4E2D).isColor == false`; an `h2d.Text` with `"A\u{1F44D}\u{1F3FD}\u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467}B"` has 2 text glyphs and 2 colour-layer glyphs.
- [ ] **Step 2: Run** — Expected: compile errors (`placeColor`, `getCluster` on a font with no `ClusterFallback`, …).
- [ ] **Step 3: Implement** bindings and Haxe classes per Interfaces; rebuild `fontsys.hdll`.
- [ ] **Step 4: Run** HeapsTest — `HeapsTest: all passed`; also re-run `fontsys_test`.
- [ ] **Step 5: Commit** (hashlink-full) — `fontsys: colour emoji glyphs and clusters for Heaps (colour pages, ClusterFallback)`.

### Task 6: Windows end-to-end in Simplified and JoeClash

**Files:**
- Modify: `D:/_github/Simplified/tests-heaps/FontFallbackTest.hx`
- Simplified `.haxelib/heaps/git`: cherry-pick Task 3 and 4 commits (`git fetch C:/HaxeToolkit/haxe/lib/heaps/joe master` then `git cherry-pick <sha>…`).

- [ ] **Step 1: Write the failing test** — in `FontFallbackTest.checkEmoji`: replace the Windows block with: for the label `EMOJI = "a\u{1F525}b\u{1F44D}\u{1F3FD}c\u{2764}\u{FE0F}"`, the text sprite's `colorGlyphs.count() == 3` and `glyphs.count() == 3` (`a`,`b`,`c`); then capture the scene (`Screen.instance.scene.captureBitmap().tile.getTexture().capturePixels()`) and assert the emoji label's row (y between `emoji.top` and `emoji.top + emoji.height`, scaled by the window scale) contains a pixel with `r > 180 && g < 140 && b < 100` (the fire's orange) — name: `the emoji are drawn in colour`.
- [ ] **Step 2: Run** the FontFallbackTest block of `test-heaps-hl.hxml` alone (copy that block into a scratch hxml, run from the Simplified root) — Expected: FAIL on Simplified's old Heaps (before cherry-pick) or on counts.
- [ ] **Step 3: Cherry-pick** the Heaps commits into `.haxelib/heaps/git`; re-run — PASS.
- [ ] **Step 4: Run the whole Simplified suite** without `HiddenPlaceListTest` (scratch copy of `test-heaps-hl.hxml` with that block removed) — Expected: every test prints its pass line; exit 0.
- [ ] **Step 5: JoeClash layout check** — build/run `tests.scenes.hxml` into a scratch log; build again with a scratch hxml that appends `-cp <scratch>/heaps-base` holding `h2d/Text.hx`, `h2d/HtmlText.hx`, `h2d/TextInput.hx`, `h2d/Font.hx`, `hxd/Charset.hx` from the Heaps commit before Task 3 (`git show <sha>~:<file>`; later `-cp` wins); diff the two logs — Expected: identical; "All text fits. OK."; peak under 50 MB. Delete the `-base` `.hl`.
- [ ] **Step 5b: Static exe still links** — in `D:/_github/_JoeClash` run `powershell -File tools/build_exe.ps1 -Static` (background; it is slow). Expected: it finishes with the exe written and no duplicate-symbol (`LNK2005`) errors. Delete nothing it built that was there before.
- [ ] **Step 6: Screenshot** — build a scratch copy of FontFallbackTest at font size 32 that saves the capture (as done for `emoji-windows.png`), view it: 🔥, 👍🏽 (one brown thumb), ❤️ in colour, untinted; save as `emoji-colour-windows.png` in the session notes folder.
- [ ] **Step 7: Commit** (Simplified) — `tests: emoji in a MainFont label draw in colour, joined`.

### Task 7: Android — rebuild native libs, test on the Pixel

**Files:**
- Modify: `C:/HaxeToolkit/haxe/lib/heaps-android/git/tools/build-hashlink-android.bat` only if the fontsys target needs a new flag (zlib is in the NDK); `thirdparty/<abi>/libhlfontsys.so` (rebuilt, all three ABIs).

- [ ] **Step 1: Native test on the device.** Build `fontsys_test` in `build-android` (arm64) and run it: `adb push` it to `/data/local/tmp/`, run with `FONTSYS_FORCE_FONTS_XML=1` (AFontMatcher crashes outside an app process) — Expected: `fontsys_test: all passed`, including the Android bitmap-emoji block (`/system/fonts/NotoColorEmoji.ttf`): the old `render_sdf … <= 0` check still holds, and add to `test_color_emoji` an `#ifdef __ANDROID__` block: shape `{0x1F44D,0x1F3FD}` → 1 and render_color → 1 with a coloured pixel, and `NotoColorEmojiFlags.ttf` shape `{0x1F1EC,0x1F1E7}` → 1, render → 1. Commit that test addition in hashlink-full.
- [ ] **Step 2: Rebuild heaps-android's libraries** with `tools/build-hashlink-android.bat` (all ABIs); record `libhlfontsys.so` arm64 size before/after.
- [ ] **Step 3: Build and install Simplified** — `haxe android.hxml` in `D:/_github/Simplified` (installs and launches on the Pixel). Confirm the log shows `buildCMakeDebug` and `Installed on 1 device`.
- [ ] **Step 4: On the Pixel**, in an incident's title field, enter `🔥👍🏽👨‍👩‍👧🇬🇧1️⃣` with the Gboard emoji panel (tap one emoji at a time, screenshot after each with `adb exec-out screencap -p`; the panel can shift under fast taps). Expected: five colour pictures; one backspace removes one whole picture. Then delete what was typed so the field is as before. Save the screenshot as `emoji-colour-pixel.png` in the session notes folder.
- [ ] **Step 5: Commit** heaps-android (`thirdparty: libhlfontsys with HarfBuzz and PNG colour emoji`) following its thirdparty README rules; note sizes.

## Done

All of: Tasks 1–7 committed locally (hashlink-full, Heaps fork, Simplified, heaps-android), Simplified desktop and Pixel show the done-sentence `🔥👍🏽👨‍👩‍👧🇬🇧1️⃣` as five colour pictures with one caret stop each, JoeClash scene-walk output unchanged, size deltas reported. Nothing pushed.
