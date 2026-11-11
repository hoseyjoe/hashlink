# Colour emoji for Heaps on HashLink — design

Date: 2026-11-11. Status: draft for review.

## Why

Joe's Heaps-on-HashLink apps (Simplified, JoeClash, EpubReader in JoeClash) draw text from SDF
fonts, with characters the app font lacks drawn from OS fonts (phase 1, `libs/fontsys`, spec
`2026-11-10-system-font-fallback-design.md`). Typing works (phase 2, IME). Since
2026-11-11 (Heaps fork `2c7a70f1`) an emoji is laid out as one character: on Windows it draws as a
flat monochrome shape (Segoe UI Emoji's outline through the SDF path), on Android as one box (the
Pixel's emoji font has no outlines). Joe wants emoji to look like they do everywhere else: **in
colour, and with sequences joined properly** — "do it right".

Done = in Simplified's Heaps builds (desktop HashLink on Windows, `android.hxml` on the Pixel), an
incident title typed as `🔥👍🏽👨‍👩‍👧🇬🇧1️⃣` shows five colour pictures, each one caret stop.

## Goals

1. Emoji draw in colour from the OS's own emoji font: Segoe UI Emoji on Windows (its COLR colour
   layers), Noto Color Emoji on Android (PNG bitmaps, `sbix` on Android 17 / `CBDT` earlier),
   Apple Color Emoji on macOS/iOS (`sbix`), Noto Color Emoji on Linux (`CBDT`). No emoji font or
   image is shipped.
2. Emoji sequences become the one picture the font has for them, using **HarfBuzz**:
   skin-tone modifiers (👍🏽), ZWJ sequences (👨‍👩‍👧), flags (regional-indicator pairs, 🇬🇧), tag
   sequences (🏴󠁧󠁢󠁳󠁣󠁴󠁿), keycaps (1️⃣), and variation selectors (❤️ vs ❤︎).
3. A joined emoji is one character for layout, line wrapping, the caret, selection, clicks and
   backspace/delete in `h2d.TextInput`.
4. If the OS font has no single picture for a sequence (an older OS than the emoji), it draws as its
   parts, the way phones do; joiners and selectors stay invisible.
5. Emoji keep their own colours whatever the text colour; they follow the text's alpha (a faded
   label fades its emoji).
6. Works through unchanged call sites: `h2d.Text`, `h2d.HtmlText`, `h2d.TextInput`, haxeui-heaps
   labels and fields, the reader (HtmlText).
7. Tested on Windows and the Pixel; macOS, iOS and Linux compile-only (as phase 1).

## Non-goals

- Shaping for other scripts (Thai, Devanagari, Arabic) and bidi. HarfBuzz is added in a way that a
  later shaping phase can reuse, but ordinary text keeps today's per-character layout.
- Animated emoji; emoji newer than the OS font; a downloadable emoji pack.
- COLRv1-only fonts (FreeType does not paint COLRv1): such a font falls back to its outline (SDF,
  monochrome) if it has one, else a box. Segoe UI Emoji has COLRv0 layers, so Windows is unaffected.
- Kha, web, .NET targets: unchanged.

## Anti-goals (a failure even if it works)

- Latin/CJK layout moving: JoeClash `SceneFitTest` output and Simplified `test-heaps-hl.hxml` must
  stay as they are (check: JoeClash's scene-walk output byte-identical before/after, as done for
  `2c7a70f1`).
- Emoji tinted by the text colour (black text → black emoji).
- Shipping an emoji font, or a large size jump: HarfBuzz is the only real addition (≈1 MB per
  native library build; report the actual numbers).
- A visible hitch when an emoji first appears, or re-rendering an emoji already seen.
- Blowing JoeClash's 50 MB scene-walk texture ceiling.
- Breaking JoeClash's static single-exe link (`tools/build_exe.ps1 -Static`), e.g. by duplicate
  libpng/HarfBuzz/FreeType symbols between `fmt` and `fontsys`.

## Facts (checked 2026-11-10/11)

- Pixel (Android 17): `/system/fonts/NotoColorEmoji.ttf` (215 MB) and `NotoColorEmojiFlags.ttf`
  (11 MB) have tables GSUB, sbix, near-empty glyf, no CBDT/COLR. FreeType with
  `FT_LOAD_NO_BITMAP` fails to load their glyphs (a miss today). A copy of the flags font is useful
  for desktop tests (pull with `adb pull`; tests take its path from an env var, as
  `FONTSYS_TEST_BITMAP_EMOJI` does now).
- Windows 11 `seguiemj.ttf`: COLR, CPAL, GSUB, glyf (monochrome outlines). FreeType renders COLRv0
  layers into a BGRA bitmap with `FT_LOAD_COLOR`.
- Vendored FreeType 2.13.3 (`include/freetype`): `TT_CONFIG_OPTION_COLOR_LAYERS` on,
  `FT_CONFIG_OPTION_USE_PNG` **off** — sbix/CBDT need it plus libpng. libpng sources are vendored
  in `include/png` and compiled into `fmt.hdll` on Windows (`libs/fmt/CMakeLists.txt`); Android's
  `fmt` gets libpng another way (check `DOWNLOAD_DEPENDENCIES`).
- FreeType's BGRA output is premultiplied; Heaps' default 2D blend is straight alpha.
- Heaps' `h3d.shader.SignedDistanceField` replaces texture RGB with white and `h2d.Text` tints via
  the drawable's colour, so a colour glyph cannot go through the SDF draw.
- `h2d.Text` / `HtmlText` / `TextInput` now walk code points (`hxd.Charset.codeAt`, `charLength`,
  `isZeroWidth`); `TextInput`'s caret helpers (`inPair`, `snapPos`, `nextCursor`, `prevCursor`,
  `charEnd`) treat a pair plus following joiners/selectors as one stop. `tests/text/EmojiTextTest`
  runs headless (stubbed engine colour cache).
- fontsys today: `fontsys_find(cp, lang, style)` → OS font path; `fontsys_render_sdf(face, cp, …)`;
  Haxe side `FontSys`, `SystemGlyphSource`, `SystemFontFallback` (EM 32, SPREAD 8), `FallbackAtlas`
  (RGBA pages, SDF in alpha, shelf packer, ≤ `maxPages`, upload once per frame).
- EpubReader's `RichText` draws through `h2d.HtmlText`; its code blocks are pre-wrapped by
  `CodeWrap.wrap` measuring `font.getChar(code).width` per code point (`RichText.hx` ~205).
- heaps-android builds `libhlfontsys.so` for arm64-v8a, armeabi-v7a and x86_64 with
  `tools/build-hashlink-android.bat` (`-DWITH_FONTSYS=ON`) into committed `thirdparty/<abi>/`.
  Simplified's `android.hxml` builds arm64-v8a only.

## Design

### Layers

```
apps            — unchanged (they already install SystemFontFallback on their fonts)
Heaps fork      — emoji clusters in layout; colour glyphs drawn on their own layer
fontsys (Haxe)  — cluster lookup; colour atlas pages; colour FontChars
fontsys (C)     — HarfBuzz: cluster → glyph id; FreeType colour render (PNG + COLRv0) → RGBA
HarfBuzz + FreeType + libpng, compiled into fontsys
```

### Heaps (fork, `master`)

1. **Clusters.** A function (e.g. in `hxd.Charset`) gives the end of the cluster starting at a
   position: one code point extended by variation selectors, emoji modifiers (U+1F3FB–1F3FF), the
   keycap mark U+20E3, tag characters (U+E0020–E007F), `ZWJ + next code point` repeatedly, and a
   second regional indicator after a first. This follows the emoji sequence grammar of UTS #51; it
   need not implement full UAX #29 grapheme clusters (combining marks for other scripts are out of
   scope). A cluster of one code point behaves exactly as today.
   **Emoji clusters vs. others.** A cluster is an *emoji cluster* when it contains U+FE0F, the
   keycap mark U+20E3, an emoji modifier, ZWJ, a regional indicator or tag characters. Every other
   cluster (including a CJK character followed by an ideographic variation selector U+E0100–E01EF,
   or by U+FE00–FE0E) keeps today's path: its first code point through `getChar`, the selectors
   invisible — so Latin/CJK layout cannot move. A single code point with no selector (e.g. a bare
   ❤ or 😀) also keeps today's path: the app font first, then the fallback.
   **The caret stops once per cluster regardless of how it draws** (as Android/ICU do), so the
   cluster rule lives purely in Heaps text code and `TextInput` never needs the font's answer.
2. **Font hook.** Emoji clusters are looked up as a whole and **always go to the fallback (the OS
   emoji font), even when the app font has the base character** — 1️⃣, #️⃣, ©️, ❤️ must not come
   out as the app font's "1", "#", "©", "♥". So the cluster lookup sits before `glyphs` /
   `charset.resolveChar` for emoji clusters only: `h2d.Font` gains a cached cluster lookup (keyed
   by the cluster's text) and `FontFallback` a matching method; single code points keep the
   existing `getChar` path (no cost for Latin). A miss (no single picture) means "draw the parts":
   each code point through `getChar` (app font first, as today), zero-width characters invisible.
3. **Colour glyphs.** A `FontChar` can be marked colour. `h2d.Text` and `h2d.HtmlText` draw colour
   glyphs on a second layer (a second `TileGroup` or equivalent) that has no SDF shader and is not
   tinted: colour white, alpha = the text's alpha. Drawn after the text glyphs. This applies to
   non-SDF fonts too (tint must not apply). Layout (advances, line breaks, `calcTextWidth`) treats
   a colour glyph like any other.
   **Drop shadow** (`Text.dropShadow`, used by JoeClash map labels and titles; HtmlText too): an
   emoji casts a silhouette shadow in the shadow colour and alpha (its alpha shape, offset), as an
   OS does — never a colour copy.
4. **TextInput.** The caret, selection, clicks, vertical moves, insert/cut, backspace/delete step by
   cluster, generalising the current pair+selector rule. The composition underline and IME
   candidate area are unaffected beyond using the same widths.
5. **Wrapping.** A cluster is never split across lines (Text and HtmlText).

### fontsys C core (plain C API, Kha-ready as before)

- **HarfBuzz** vendored as release source (amalgamated `harfbuzz.cc`) in `include/harfbuzz`,
  compiled into `fontsys` like FreeType, symbols hidden as FreeType's are. Built against the
  vendored FreeType (`hb-ft`). Size options (e.g. `HB_NO_*`/`-Os`) are the builder's call within
  ≈1 MB per build, keeping what a later full-shaping phase needs.
- **Cluster → glyph:** a new call shapes a cluster (code points) with a face and returns the glyph
  id if HarfBuzz produced exactly one glyph, else "no single picture".
- **Find:** finding the font for an emoji cluster must return the OS **emoji** font even when the
  first code point is a digit, `#`, `*`, ©, ® or ™ (keycaps, ©️): an "emoji" request mode per
  platform (e.g. pass the whole cluster where the OS API takes text — DirectWrite
  `MapCharacters`, `AFontMatcher_match`, CoreText; fontconfig with `color=true`/emoji family).
  Regional indicators find the flags font on Android.
- **Colour render:** a new call renders a glyph id (single code points use their cmap glyph) with
  `FT_LOAD_COLOR` to straight-alpha RGBA at a requested size: bitmap strikes (sbix/CBDT, via
  libpng) are picked at the nearest strike and scaled down with a good filter; COLRv0 is rendered
  at the size. It reports whether the result is colour, plus metrics for normalising (bitmap
  size, bearing, advance, the strike's ppem).
- **PNG:** `FT_CONFIG_OPTION_USE_PNG` on, libpng compiled in or linked so that JoeClash's static
  exe still links (no duplicate `png_*` symbols with `fmt`) — builder's call how.
- **Which path:** a face with colour data (`FT_HAS_COLOR`) and a glyph with colour → colour path.
  Text presentation (U+FE0E) → the existing SDF outline path when the font has an outline,
  otherwise colour. Everything that is not emoji keeps the SDF path unchanged.

### fontsys Haxe side

- `SystemFontFallback` resolves clusters (cache keyed by cluster text, language, style) and single
  code points from colour faces as colour `FontChar`s.
- Colour glyphs are rendered once at **64 px** (Claude's call) into **RGBA colour pages** owned by
  `FallbackAtlas` (or a sibling), separate from SDF pages, created on first use; a page cap with a
  logged "full" protects the budget (default cap: builder's call, within JoeClash's ceiling — a
  1024² page is 4 MB, ~250 emoji).
- Metrics: an emoji is sized to the base font like an OS does it (the font's em; a strike of ppem
  P maps P pixels to one em), baseline-aligned like the SDF fallback glyphs, scaled per `Font`
  size like them.

### Builds

- this repo: CMake, Makefile and VS (`fontsys.vcxproj`) build HarfBuzz (C++) and libpng into or
  alongside fontsys; Windows desktop `fontsys.hdll`; static `.hdll.lib` for JoeClash `-Static`.
- heaps-android: rebuild `libhlfontsys.so` (all three ABIs) with HarfBuzz and PNG via
  `tools/build-hashlink-android.bat`; commit per the thirdparty README rules.
- Simplified: cherry-pick the Heaps commits into `.haxelib/heaps/git` (as for `2c7a70f1`); its
  `.haxelib/hlfontsys` is a dev link to this repo.

## Testing

- Heaps headless (`tests/text`): cluster segmentation for each sequence kind (UTS #51 examples),
  layout with a fake fallback returning colour clusters (one glyph per cluster, on the colour layer,
  untinted), wrapping never splits a cluster, caret/backspace by cluster, misses draw as parts.
- fontsys native (`fontsys_test`): Segoe UI Emoji — 👍🏽 and 👨‍👩‍👧 shape to one glyph each, colour
  render has non-grey pixels; a pulled Noto Color Emoji flags font (env var) — 🇬🇧 shapes to one glyph
  and decodes to colour; existing tests unchanged.
- fontsys Heaps test: a real OS emoji comes back as a colour glyph on a colour page.
- Simplified `FontFallbackTest`: the emoji label's glyphs are colour, and a capture shows coloured
  pixels; the rest of `test-heaps-hl.hxml` passes.
- JoeClash `SceneFitTest`: output identical to before; texture peak under the ceiling.
- On the Pixel and on Windows: type the done-sentence into a Simplified incident title; screenshot.
- Report the size of `fontsys.hdll` and `libhlfontsys.so` (arm64) before and after.

## Decided (with Joe)

- Colour emoji, from OS fonts, nothing shipped.
- Join sequences properly; HarfBuzz rather than a small emoji-only reader (≈1 MB per build accepted).
- Platforms and testing as phase 1 (Windows + Pixel tested).

## Calls made by Claude (open to challenge)

- HarfBuzz used only for emoji clusters now; ordinary text layout unchanged.
- Emoji ignore text colour but follow its alpha.
- 64 px colour glyphs; slightly soft when drawn much larger.
- COLRv1-only fonts not painted (outline or box).
- FE0E (text presentation) prefers the monochrome outline.
- Cluster rules from UTS #51 emoji sequences, not full UAX #29.
- Emoji clusters (with FE0F, keycap, modifier, ZWJ, regional indicator or tags) always come from
  the OS emoji font, even when the app font has the base character; everything else, including a
  bare emoji code point and CJK + variation selector, keeps "app font first" (layout unchanged).
- One caret stop per cluster, whether it draws as one picture or as parts.
- Emoji cast a silhouette drop shadow, never a colour one.

## Left to the builder

- API names and signatures (C, HL bindings, Heaps hook, `FontChar` colour marker).
- How the colour layer is attached to `Text`/`HtmlText` (second TileGroup, draw order details).
- How libpng reaches FreeType without clashing with `fmt` in static links.
- HarfBuzz version and size options; colour page cap; downscale filter.
- Whether `CodeWrap.wrap` in EpubReader needs cluster-aware measuring (code blocks with emoji).
- Whether haxeui-heaps has its own per-character measuring that needs the same change.
