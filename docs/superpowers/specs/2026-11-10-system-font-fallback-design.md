# System font fallback for Heaps on HashLink — design

Date: 2026-11-10. Status: draft for review.

## Why

Shipping a font for every language makes an app's download big. CJK alone is ~15–20 MB per weight
as a TTF, and worse as a pre-baked Heaps atlas. Every desktop and mobile OS the apps target already
has fonts that cover those scripts. The goal: **adding a language costs a translation file, not a
bigger font — and text people type in any language just shows up.** The app ships its own (small,
branded) font; any character that font lacks is drawn from whatever font the OS has for it.

This is what Avalonia does — and both apps' .NET builds (haxeui-cs on Avalonia) already get it for
free. Heaps on HashLink has nothing equivalent: text comes only from pre-built bitmap/SDF atlases.

No languages are committed. The point is to keep the options open.

## Who it's for, and the acceptance test

- **JoeClash** (`D:\_github\_JoeClash`) — the first consumer, game UI plus the in-app reader.
- **Simplified** (`D:\_github\Simplified`, the RCA Facilitator) — **the final test.** It's a HaxeUI
  app whose users type free text (incident titles, causes, notes) in whatever language they write in,
  and it should run with its UI in different languages. Its Heaps builds: `heaps-hl.hxml` (desktop
  HashLink) and `android.hxml` (heaps-android). Done = Simplified's Heaps builds display *and accept
  typed* text in Simplified Chinese, Japanese, Korean, Cyrillic and Greek, with no font shipped for
  them — which needs phase 2 (typing) as well as phase 1 (showing).

### Facts, checked 2026-11-10

JoeClash:

- **Game font.** `Main.loadFont()` / `buildFont()` (`src/Main.hx` ~4900) loads `res/GameFont.fnt` via
  `hxd.res.BitmapFont.toSdfFont(Const.FONT_SIZE, SDFChannel.Alpha, 0.5, -1)`. 65 `h2d.Text`/`HtmlText`
  construction sites use it. HaxeUI uses the same atlas through `view/ChatToolkit.installFont()` →
  `haxe.ui.backend.heaps.SDFFonts.register("GameFont.fnt", Alpha, 0.5, -1)`.
- **The reader's eight faces.** The in-app reader (epubreader haxelib, `D:\_github\EpubReader`) has its
  own `ReaderFonts`: eight SDF faces (`res/fonts/reader-*.fnt` — regular/bold/italic/bolditalic and
  four mono — 1024×1024 and 1024×2048, copied in by `src/ReaderFontSync.hx`), loaded with `toSdfFont`
  and registered with `SDFFonts`. `reader.ui.TextFallback` substitutes ASCII wherever
  `ReaderFonts.hasGlyph` (`font.hasChar`) is false. `ReaderFonts.release()` disposes the atlases on
  teardown.
- **GameFont atlas:** Latin-1 plus a few symbols, baked by `tools/gen_sdf_font.py` from
  `art-src/font/GameFont.ttf` at EM 48, spread 12, distance in the alpha channel. `GameFont.png` is
  512×2048 RGBA (4 MB); ~806 rows free — room for only ~60 CJK cells at that size.
- **Locales:** `locale/en.properties`, `locale/fr.properties`. `tests/TestContent.hx`'s
  `testFontCoversEveryLocaleGlyph` fails any locale using a character the atlas lacks.
- **`tests/SceneFitTest.hx`** links heaps for real metrics, walks every screen (reader included) in
  every locale, and caps the walk's peak texture memory at **50 MB** (`TEX_CEILING_MB`), fonts
  included; its notes put the peak at ~33–35 MB, so ~15 MB is spare.
- **Dead fallback today:** steps 2–3 of `buildFont()` (`fonts/GameFont.ttf`, then
  segoeui/calibri/arial via `hxd.res.Font.build`) always throw on HashLink — Heaps'
  `hxd/res/FontBuilder.hx` is `#if js … #else throw` — and the `catch` swallows it.

Simplified:

- Same pattern: `src/Main.hx` registers `MainFont.fnt` (SDF, `Alpha`, generated from
  `fonts/MainFont.ttf` the same way) with `SDFFonts`; `AppTheme` sets `.component { font-name: "MainFont.fnt" }`.
- Its heaps hxmls add `rca.macro.HeapsText.wordBreakByDefault()` to `h2d.Text`. CJK line breaking is
  already in Heaps (`hxd.Charset.isCJK` / `isBreakChar`, plus the kinsoku `complementChars` table).
- Its UI is not localized yet; localizing it is not part of this design.

Shared:

- Heaps dev checkout: `C:/HaxeToolkit/haxe/lib/heaps/joe`, branch `master` (the `hoseyjoe` fork).
  haxeui-heaps dev checkout: `C:/HaxeToolkit/haxe/lib/haxeui-heaps/joe`. haxeui-heaps clones and
  `resizeTo`s fonts per text size (`TextDisplayImpl`, `HtmlLabelText`, `TextInputImpl`), and
  `BitmapFont.toSdfFont` caches a separate `Font` per size.
- `h2d.Text` builds one `TileGroup` from `font.tile` — **a single texture** — and
  `h3d.mat.Texture` only uploads whole textures (`uploadPixels`).
- `libs/sdl/sdl.c` drops IME composition and truncates committed text (see phase 2).
- Builds: HashLink JIT, HL/C single exe (JoeClash `tools/build_exe.ps1`), HL/C Android via
  heaps-android (`android-shell` **minSdk 24**).

## Goals

1. Any character an app's own font lacks is drawn from an installed OS font, on demand, in the same
   SDF style (same shader, same `SDFChannel`/cutoff/smoothing), size- and baseline-matched.
2. **Unbounded glyph count.** Typed text can contain any of thousands of CJK characters; the
   fallback must keep working past one texture's worth.
3. Works through unchanged *call sites*: `h2d.Text`, `h2d.HtmlText`, `h2d.TextInput`, and
   haxeui-heaps labels and text fields. Heaps itself changes (below); the apps change only where they
   create fonts.
4. Every SDF face gets fallback, **including the reader's eight**, with the OS font's weight and slant
   matched to the face (bold → bold, italic → italic, mono → a monospace face where the OS has one).
5. Platforms: Windows, macOS, Linux, Android, iOS. **Tested** on Windows and Android; macOS, Linux and
   iOS are **compile-only, untested** in this work (machines exist; testing them is later work).
6. Scripts that render correctly one code point at a time: Latin, Cyrillic, Greek, CJK, and anything
   else that needs no shaping. Han glyphs follow the app's language (Simplified vs. Traditional
   Chinese vs. Japanese vs. Korean forms).
7. Characters no installed font covers draw as the font's `defaultChar` (a box), looked up once.
8. **Kha-ready layering:** the native core is plain C with no HashLink types; HashLink bindings are a
   thin separate file.

## Non-goals

- **Shaping** (HarfBuzz): Thai, Devanagari etc. render wrongly. Later phase; changes `h2d.Text`.
- **Right-to-left / bidi.** Not planned; must not be designed out.
- **Kha, consoles, web (heaps-js), .NET** — unchanged. Only the C-core boundary is kept for Kha.
- Choosing the nicest OS font: fallback takes what the OS's own fallback API returns.
- Localizing Simplified's UI strings.

## Anti-goals (a failure even if it works)

- Shipping a large font or atlas anyway.
- Fallback glyphs that visibly don't match: bitmap-looking glyphs, size or baseline jumps on a mixed
  line, regular glyphs inside bold or italic text.
- Re-querying the OS or re-rasterizing every frame for a character already seen (or seen missing).
- Changing Latin layout: `en`/`fr` `SceneFitTest` results must not move.
- Blowing JoeClash's 50 MB scene-walk texture ceiling.
- A typing hitch bad enough to notice when a new character appears.

## Design

### Layers

```
apps (JoeClash Main/ChatToolkit/ReaderFonts, Simplified Main) — install fallback on the fonts they make
Heaps fork: h2d.Font hook + multi-page text                   — ask a fallback for missing chars; draw glyphs from several textures
fontsys Haxe side (hl only)                                   — the fallback: pages, packing, FontChar creation, caches
libs/fontsys/fontsys_hl.c                                     — HashLink bindings (HL_PRIM), thin
libs/fontsys/fontsys.c + fontsys_<os>.c                       — plain C core: find a font for a code point; rasterize SDF
FreeType ≥ 2.11 (compiled in)                                 — face loading and SDF rasterization (FT_RENDER_MODE_SDF)
```

### Native core (plain C, Kha-ready)

New library in this repo: `libs/fontsys/` → `fontsys.hdll`.

1. **Find:** given a code point, a **language tag** (BCP 47: `zh-Hans`, `zh-Hant`, `ja`, `ko`, …), and
   a **style** (weight, italic, monospace), return a font the OS says can draw it — file path plus face
   index, or font bytes where paths aren't available.
   - **Windows:** DirectWrite `IDWriteFontFallback::MapCharacters` (Win 8.1+) → face `GetFiles` → path.
   - **macOS / iOS:** CoreText `CTFontCreateForString` (with language and traits) → `kCTFontURLAttribute`.
   - **Linux:** fontconfig: pattern with `FcCharSet`, `FC_LANG`, weight/slant/spacing →
     `FcFontSort`/`FcFontMatch` → path + index.
   - **Android:** NDK `AFontMatcher` (API 29+) → `AFont_getFontFilePath`. API 24–28 needs a fallback,
     e.g. parsing `/system/etc/fonts.xml` and testing coverage with `FT_Get_Char_Index`.
2. **Rasterize:** open the face with FreeType (open faces cached), render one glyph as a single-channel
   SDF bitmap (`FT_RENDER_MODE_SDF`) at a fixed authoring size, return the bitmap plus metrics
   (advance, bearings, size) and the face's em/ascent so the caller can normalize.

No HashLink types; state lives in a context handle. `fontsys_hl.c` maps it to `HL_PRIM` functions in
the style of `libs/sdl/sdl.c` / `libs/fmt/fmt.c`. FreeType's sources compile straight into the
`fontsys` target (no separate lib), and Windows system libs (`dwrite.lib`) come in via
`#pragma comment(lib, …)`, so JoeClash's `-Static` exe link needs no edit.

### Heaps changes (fork, `master`)

**a. Fallback hook in `h2d.Font`.** `getChar` is `inline` and only asks `charset.resolveChar(code,
glyphs)` on a miss; `charset` is shared across clones and isn't told which font (size) is asking — so
`Charset` can't be the hook. Add a public fallback field (an interface, e.g.
`resolve(font : Font, code : Int) : Null<FontChar>`), consulted **after** `charset` (so `hxd.Charset`'s
existing mappings — curly quotes → `"`, NBSP → space, full-width ASCII → ASCII — still come from the
app font and Latin layout is unchanged), its result stored in `glyphs`; copied in `clone()`. Also a
way to ask "can this font draw `code`, counting fallback?" for callers like the reader's
`TextFallback`, which today use `hasChar`.

**b. Multi-page text — already supported.** Goal 2 rules out one shared texture. Checked while
planning: Heaps' `TileGroup` starts a new draw state whenever a quad's texture changes
(`h2d/impl/BatchDrawState.hx` `setTexture`), so `h2d.Text`, `HtmlText` and `TextInput` already draw
glyphs from several textures — one draw call per texture run. No change is needed. The SDF shader
setup is per `Text`, so every page must be sampled with the base font's `SDFChannel`.

(a) is a small, upstreamable patch.

### The fallback (Haxe, HashLink only)

One fallback per base face (JoeClash: GameFont + 8 reader faces; Simplified: MainFont), each with its
style (weight/italic/mono) and the app's current language.

- **Pages:** fallback glyphs go into fallback-owned atlas pages (e.g. 1024²), created on demand, not
  into the app font's PNG. Pages may be shared between faces. A page's format must give the right value
  in the base font's channel (`Alpha` today): RGBA pages, or move the base fonts to `Red` and use R8
  pages — builder's call within the texture budget.
- **Glyph cache** keyed by (code point, language, style), stored once at the authoring size. For each
  requesting `Font`, the fallback creates a `FontChar` whose tile is a sub-tile of a page scaled by
  `font.size / authoringSize` — the same scaling `resizeTo` applies to baked glyphs.
- **Metrics:** baseline aligned to the base font's baseline; size normalized to the base font's em, so
  a CJK glyph next to Latin doesn't jump.
- **Upload:** CPU copy of each page; mark dirty when glyphs are added; re-upload each dirty page at
  most once per frame.
- **Misses** cached; resolve to `defaultChar`.
- **Lifetime:** pages outlive any one `Font` (so `ReaderFonts.release()` disposing reader atlases
  doesn't lose them); a fallback can be cleared explicitly. No eviction in this work; a page cap with
  a logged "full" (then `defaultChar`) protects the budget.
- **Language switch at runtime:** new lookups use the new language; already-packed glyphs stay as
  drawn.

### App wiring

- **JoeClash:** `Main.buildFont()`, `ChatToolkit.installFont()` and epubreader's `ReaderFonts` install
  fallbacks on the fonts they create. Because haxeui-heaps derives fonts with `toSdfFont` and clones
  them, every `Font` `toSdfFont` returns for a registered resource must carry the fallback (builder's
  call how — e.g. a registry `toSdfFont` consults). `reader.ui.TextFallback` switches to the
  "can draw, counting fallback" query. `testFontCoversEveryLocaleGlyph` keeps checking the brand font
  for characters the game draws itself; locales meant to come from OS fallback are exempted
  explicitly in the test. `SceneFitTest` metrics for such locales depend on the machine; noted there.
- **Simplified:** `Main` installs the fallback on `MainFont.fnt`.
- `#if hl` only; other targets behave exactly as today.

## Phase 2 — typing text (needed for the final test)

- `libs/sdl/sdl.c`: `SDL_EVENT_TEXT_EDITING` is dropped (`// skip`, ~line 305), and
  `SDL_EVENT_TEXT_INPUT` packs committed text into a 4-byte `keyCode` (~line 308), so a multi-character
  IME commit ("你好世界") arrives as its first character only. Pass composition (text, cursor,
  selection) and full committed strings through as new event data.
- Call `SDL_SetTextInputArea` so the IME candidate window sits at the focused text field.
- Heaps: `hxd.Event`/`hxd.Window` carry the new data; `h2d.TextInput` shows composition inline
  (underlined) and inserts the full committed string.
- haxeui-heaps `TextInputImpl` passes it through; Simplified's text fields work with Windows IME and
  Android's soft keyboard (Gboard/pinyin).

## Later phases (designed for, not built)

- **Shaping:** HarfBuzz in the C core; an `h2d.Text` path that lays out shaped glyph runs.
- **Right-to-left / bidi.**
- **Kha:** wrap the C core in Kore; a Kha-side fallback for `joekha.SdfTextRenderer`.
- Testing on macOS, Linux, iOS; page eviction.

## Constraints

- Builds:
  - this repo: CMake/Makefile/VS (`hl.sln` + `fontsys.vcxproj`). On Windows `fmt` compiles vendored
    sources from `include/`; `DOWNLOAD_DEPENDENCIES` is off by default except on Android.
  - JoeClash `tools/build_exe.ps1 -Static`: static `.hdll.lib`s carry no dependency record; avoided
    by compiling FreeType in and `#pragma comment(lib, …)` (above).
  - heaps-android: `thirdparty/` holds prebuilt `.so` files committed to git (~50 MB of history per
    rebuild), produced by `tools/build-hashlink-android.bat` with explicit `-DWITH_*` flags. A new
    library needs: that script, an `IMPORTED` entry and `target_link_libraries` in
    `android-shell/app/src/main/cpp/CMakeLists.txt`, `MainActivity.kt` `getLibraries()`, and the
    thirdparty README's "same 9 .so files" rule.
  - macOS `.hdll`s stay relocatable (`set_as_hdll` already handles it).
- Texture budget: fallback pages count toward JoeClash's 50 MB walk ceiling (~15 MB spare, shared with
  the reader). A 1024² page is 4 MB RGBA, 1 MB R8.
- Builds that don't install a fallback behave exactly as today.

## Decided (with Joe)

- HashLink + Heaps only for now; Kha maybe later → plain C core separate from HL bindings.
- Platforms Windows, macOS, Linux, Android, iOS; tested on Windows + Android, the rest compile-only.
- No RTL; no committed languages.
- Typed text in any language is required; Simplified is the final test.
- The in-app reader is in scope.

## Calls made by Claude (open to challenge)

- FreeType everywhere rather than each OS's renderer; compiled into `fontsys`.
- No HarfBuzz in phases 1–2.
- Hook in `h2d.Font`, after `Charset`.
- Multi-page text in `h2d.Text` (rather than eviction or one huge atlas), because typed text makes the
  glyph count unbounded.
- Fallback pages separate from the app fonts' PNGs (no re-bake of GameFont/MainFont required).
- Han forms follow the app's current language; no re-draw on a runtime switch.
- Box (`defaultChar`) when nothing covers a character; no downloadable font pack.
- Library name `fontsys`, in `libs/fontsys/`; this spec lives in this repo.
- Reading "Simplified will be the final test" as the Simplified app (`D:\_github\Simplified`).

## Left to the builder

- Fallback interface names/signatures; how fallbacks reach `toSdfFont` fonts.
- Page size, format (RGBA vs. Red/R8), packing algorithm and cap, within the budget.
- Authoring size for fallback glyphs (EM 48 matches GameFont) and FreeType SDF spread to match
  `gen_sdf_font.py`'s `SPREAD = 12`.
- Android API 24–28 font discovery.
- iOS: whether system font files are readable via the CoreText URL; else font bytes from CoreText
  tables. (Compile-only here, but the code path must exist.)
- Whether to also make `hxd.res.Font.build` work on HashLink with the same library (would revive
  `buildFont()`'s dead TTF steps).
