# IME Text Input (system font fallback phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Users can type Chinese, Japanese and similar IME text into `h2d.TextInput` / HaxeUI text fields in Heaps-on-HashLink apps, with inline composition on desktop and whole commits everywhere.

**Architecture:** hlsdl passes SDL3's commit and composition text as strings and gains per-window text-input control. Heaps decodes them into `ETextInput` (one per code point) and a new `ETextComposition`, routes them to the focused Interactive, and `h2d.TextInput` draws the composition display-only, keeps keys away from the IME, places the candidate window and starts/stops text input with focus. haxeui-heaps keeps its caret, selection and password mask right while composing.

**Tech Stack:** C (SDL3 3.4.x, `HL_PRIM`), Haxe 4.3.7, HashLink JIT + HL/C, Heaps (hoseyjoe fork), haxeui-heaps (fork), heaps-android, MSBuild (`hl.sln`), Android NDK.

**Spec:** `docs/superpowers/specs/2026-11-10-ime-text-input-design.md`

## Global Constraints

- SDL3 3.4.x as vendored; don't patch SDL. heaps-android's existing `tools/patch-sdl-java.ps1` edit ("one text event per character") stays — it already avoids the Android cut; the `sdl.c` fix makes it redundant, not wrong.
- Composition is display-only: never in `text`, `onChange`, undo, or a HaxeUI field's value.
- Latin typing on a normal keyboard behaves exactly as before (keys, shortcuts, undo, selection).
- A lone surrogate never ends up in a field's text.
- Fonts with **no** `fallback` keep today's `hasChar` filter unchanged.
- Repos/branches: hashlink-full `master` (`D:\_github\hashlink-full`), Heaps `C:/HaxeToolkit/haxe/lib/heaps/joe` `master`, haxeui-heaps `C:/HaxeToolkit/haxe/lib/haxeui-heaps/joe`, heaps-android `C:/HaxeToolkit/haxe/lib/heaps-android/git`, Simplified `D:\_github\Simplified`. Commit per task in the repo it touches. **Nothing is pushed.**
- HashLink install used by tests: `C:/SDKs/hashlink` (holds `hl.exe`, `sdl.hdll`, `SDL3.dll`). Before the first copy, back up the shipped `sdl.hdll` to `C:/SDKs/hashlink/sdl.hdll.pre-ime`.
- Platforms: everything compiles for Windows, macOS, Linux, Android, iOS; tested on Windows and Android only.

## Review Focus

1. **Composition that wraps in a multi-line field** (HaxeUI TextArea, narrow width): underline drawn on both lines, caret on the right line, value untouched → test in Task 3.
2. **Long single-line title scrolled horizontally**: candidate-window `cursor` offset must subtract `scrollX`, or the list opens off to the right → test in Task 3.
3. **An empty composition with nothing composing** (Windows sends one at start and after a commit): no change, no `onChange`, no cursor move → test in Task 3.
4. **A commit containing `\n`/`\r`/other control characters** (Gboard clipboard chip, paste-like commits): a single-line field drops them; a multi-line field turns `\r\n`/`\r` into `\n` → test in Task 3.
5. **Late composition events after blur** (an IME flushing after `SDL_StopTextInput`): ignored, field unchanged → test in Task 3.

---

### Task 1: hlsdl — whole text, composition events, per-window text-input control

**Files:**
- Modify: `libs/sdl/sdl.c` (event enum, `event_data`, `SDL_EVENT_TEXT_INPUT`/`SDL_EVENT_TEXT_EDITING` cases, new prims), `libs/sdl/sdl/Event.hx`, `libs/sdl/sdl/Window.hx`, `libs/sdl/sdl/Sdl.hx`
- Create: `libs/sdl/test/TextEventTest.hx`, `libs/sdl/test/text.hxml` (`-cp libs/sdl/test -lib hlsdl -main TextEventTest -hl bin/sdl-text-test.hl` then `--cmd hl bin/sdl-text-test.hl`)

**Interfaces:**
- Produces (Haxe, `sdl.Event`): `var text : hl.Bytes` (NUL-terminated UTF-8, GC copy; null for other events), `var editStart : Int`, `var editLength : Int` — appended after `dropFile` in this order, mirrored by `vbyte* text; int editStart; int editLength;` at the end of `event_data`. `EventType.TextEditing = 10` (C enum: `TextEditing` after `TextInput`).
- Produces (`sdl.Window` methods): `startTextInput() : Void`, `stopTextInput() : Void`, `isTextInputActive() : Bool`, `setTextInputArea(x:Int, y:Int, w:Int, h:Int, cursor:Int) : Void` — arguments in **window pixels**; the prim divides by `SDL_GetWindowPixelDensity(win)` before `SDL_SetTextInputArea`. Prims: `win_start_text_input`, `win_stop_text_input`, `win_text_input_active`, `win_set_text_input_area`.
- Produces (`sdl.Sdl`): `static function debugPushTextEvent(editing:Bool, utf8:String, start:Int, length:Int) : Bool` → prim `debug_push_text_event` (test-only; the C side `SDL_strdup`s the text and leaks it).
- `keyCode` keeps its packed value for `TextInput` (back-compat). `editStart`/`editLength` are SDL's raw values (UTF-16 units on Windows, may be -1).

- [ ] **Step 1: Write the failing test** `TextEventTest.hx`: `Sdl.init()`, create a hidden `sdl.Window`, then for each case push with `debugPushTextEvent` and pump with the same loop hxd uses (`Sdl.eventLoop(e)` until it returns false), checking the event of the expected type:
  - commit `"你好世界"` → `type == TextInput`, `String.fromUTF8(text) == "你好世界"`.
  - commit `"😀"` → decoded string length 2 (one surrogate pair) and `charCodeAt(0) == 0xD83D`.
  - commit `"a"` → `keyCode == 0x61` (back-compat).
  - editing `"nihao"`, start 2, length -1 → `type == TextEditing`, text `"nihao"`, `editStart == 2`, `editLength == -1`.
  - editing `""`, 0, 0 → `TextEditing` with empty text (not null).
  - window: `stopTextInput()` → `isTextInputActive() == false`; `startTextInput()` → `true`; `setTextInputArea(10, 20, 300, 24, 40)` does not throw.
  Print `TextEventTest: all passed` and exit 0, else print each failure and exit 1.
- [ ] **Step 2: Run to verify it fails** — `haxe libs/sdl/test/text.hxml` → compile error (`debugPushTextEvent`/`text` unknown).
- [ ] **Step 3: Implement** the enum value, struct fields, the two event cases (copy with `hl_copy_bytes(e.text.text, strlen+1)` as `SDL_EVENT_DROP_FILE` does; `TEXT_EDITING` sets `window`, `text`, `editStart = e.edit.start`, `editLength = e.edit.length`), and the five prims (`DEFINE_PRIM` lines next to the existing `text_input`).
- [ ] **Step 4: Build and install** — `msbuild hl.sln /p:Configuration=Release /p:Platform=x64 /t:sdl` → `x64/Release/sdl.hdll`; back up `C:/SDKs/hashlink/sdl.hdll` to `sdl.hdll.pre-ime` (first time only), copy the new one over it.
- [ ] **Step 5: Run to verify it passes** — `haxe libs/sdl/test/text.hxml` → `TextEventTest: all passed`, exit 0.
- [ ] **Step 6: Commit** — `git add libs/sdl && git commit -m "sdl: pass text input/editing as strings; per-window text input control"`

### Task 2: Heaps — event decoding, routing, window text-input API

**Files:**
- Create: `hxd/impl/ImeText.hx`, `tests/ime/ImeTextTest.hx`, `tests/ime/build.hxml` (`-cp tests/ime -cp . -main ImeTextTest -hl bin/ime-test.hl`, `--cmd hl bin/ime-test.hl`)
- Modify: `hxd/Event.hx`, `hxd/Window.hl.hx`, `hxd/Window.hx`, `hxd/Window.js.hx`, `hxd/SceneEvents.hx` (lines ~153 and ~331), `h2d/Interactive.hx` (filter ~135, dispatch ~216, new delegate)

**Interfaces:**
- Consumes: Task 1's `sdl.Event.text/editStart/editLength`, `TextEditing`, `sdl.Window` text-input methods.
- Produces:
  - `hxd.EventKind.ETextComposition` (appended last); `hxd.Event` fields `composition : String`, `compositionCursor : Int`, `compositionLength : Int` (UTF-16 indices into `composition`; `toString` prints them).
  - `hxd.impl.ImeText.codePoints(s:String) : Array<Int>` (surrogate pairs combined; a lone surrogate is passed through as itself).
  - `hxd.impl.ImeText.toUtf16Index(s:String, raw:Int, rawIsUtf16:Bool) : Int` — `raw < 0` → `s.length`; clamps to `[0, s.length]`; never returns an index between a pair.
  - `hxd.impl.ImeText.toUtf16Length(s:String, start:Int, raw:Int, rawIsUtf16:Bool) : Int` — `raw < 0` → 0.
  - `hxd.Window` (all targets): `startTextInput() : Void`, `stopTextInput() : Void`, `setTextInputArea(x:Int, y:Int, w:Int, h:Int, cursor:Int) : Void` (window pixels), read-only `textInputActive : Bool`, read-only `textInputArea : Null<{x:Int, y:Int, w:Int, h:Int, cursor:Int}>` (last area sent; `setTextInputArea` with the same values sends nothing). Non-hlsdl targets: state is tracked, nothing is sent.
  - `h2d.Interactive.onTextComposition(e:hxd.Event) : Void` (dynamic; only fired when focused, like `onTextInput`).
- hlsdl `hxd.Window` constructor: `sdl.Sdl.setHint("SDL_IME_IMPLEMENTED_UI", "composition")` before `new sdl.Window(...)`, and `stopTextInput()` right after.
- `TextInput` decoding: one `ETextInput` per code point of `String.fromUTF8(e.text)` (fall back to the old `keyCode` unpacking when `e.text == null`). `TextEditing` → one `ETextComposition`; `rawIsUtf16` is `true` on Windows (`hxd.System.platform == PC && Sys.systemName() == "Windows"`), else `false`.

- [ ] **Step 1: Write the failing test** `ImeTextTest.hx` (no window):
  - `codePoints("a你😀") == [0x61, 0x4F60, 0x1F600]`.
  - `toUtf16Index("a😀b", 2, false) == 3`; `toUtf16Index("a😀b", 2, true) == 2`; `toUtf16Index("a😀b", -1, true) == 4`; `toUtf16Index("😀", 1, true) == 2` (never mid-pair); `toUtf16Index("ab", 9, false) == 2`.
  - `toUtf16Length("a😀b", 1, 1, false) == 2`; `toUtf16Length("ab", 0, -1, true) == 0`.
  - `new hxd.Event(ETextComposition).toString()` contains `composition=`.
- [ ] **Step 2: Run to verify it fails** — `haxe tests/ime/build.hxml` → compile error (`hxd.impl.ImeText` unknown).
- [ ] **Step 3: Implement** `ImeText`, the `Event` additions, the `Window` API/decoding/hint/startup stop, and the routing in `SceneEvents` and `Interactive`.
- [ ] **Step 4: Run to verify it passes** — `haxe tests/ime/build.hxml` → `ImeTextTest: all passed`; also `haxe tests/font/build.hxml` still passes.
- [ ] **Step 5: Commit** — `git add hxd h2d/Interactive.hx tests/ime && git commit -m "hxd: whole IME commits, ETextComposition, window text-input API"`

### Task 3: `h2d.TextInput` — composition, keys, commit, focus, candidate area

**Files:**
- Modify: `h2d/TextInput.hx` (Heaps fork)
- Create (Simplified): `tests-heaps/TextInputImeTest.hx`; Modify: `test-heaps-hl.hxml` (one `--next` block in the existing style: `-cp tests-heaps`, `-main TextInputImeTest`, libs `heaps hlsdl haxeui-core haxeui-heaps heaps-drive`, `-D drive`, `-hl build/test-heaps/TextInputImeTest.hl`, `--cmd hl …`)

**Interfaces:**
- Consumes: Task 2's event kind/fields, `Interactive.onTextComposition`, `hxd.Window.startTextInput/stopTextInput/setTextInputArea/textInputActive/textInputArea`.
- Produces (`h2d.TextInput`):
  - `public var composition(default, null) : String = ""`, `compositionCursor(default, null) : Int`, `compositionLength(default, null) : Int`.
  - `public function isComposing() : Bool` (`composition != ""`).
  - `public function commitComposition() : Void` — inserts the composition as typed text (one undo step, `onChange` once) and clears it; no-op when not composing.
  - `public function displayText() : String` — `text` with the composition spliced in at `compositionPos` (raw text position fixed when the composition started).
  - `public dynamic function onTextComposition(e:hxd.Event) : Void` delegate.
  - While composing, `cursorIndex` is the caret **in the displayed text** (`getCursorPos(compositionPos + compositionCursor)` over `displayText()`); when it ends, it returns to `getCursorPos(compositionPos)` over `text`.

Behaviour to implement (decided; the spec's Goals 1–5):
- Layout helpers read `displayText()` instead of `text`: `getSplitLines`, `getTextPos`, `getCursorPos`, `getCursorLine`, and the glyph build (`initGlyphs` override substitutes `displayText()` when its argument is `this.text`). The `cursorText` check in `draw` compares `displayText()`. A composition change marks the text for rebuild (`needsRebuild`, `calcDone = false`, `splitLines = null`, `onContentChanged()`).
- Underline: after `super.draw`, a 1 px tile (text colour) under the composition on each display line it spans, 2 px under `[compositionCursor, compositionCursor + compositionLength)` when `compositionLength > 0`, at the baseline (`font.baseLine + 1`).
- `ETextComposition`: ignored unless focused and `canEdit`; empty + not composing → nothing at all; first non-empty one cuts a selection (`beforeChange`, `cutSelection`, `onChange`) and fixes `compositionPos`; empty → composition cleared, cursor restored, no `onChange`.
- `ETextInput`: if composing, clear it first (as above), then insert as today. Character filter: `font.fallback == null` → today's `hasChar` check; with a fallback → accept any code point ≥ 0x20 except 0x7F. Control characters: single-line drops `\r`/`\n`/others < 0x20; multi-line keeps `\n`, turns `\r` (alone or before `\n`) into one `\n`.
- `interactive.onKeyDown` while composing: for BACKSPACE, DELETE, LEFT, RIGHT, UP, DOWN, HOME, END, PGUP, PGDOWN, ENTER, NUMPAD_ENTER, TAB, ESCAPE, and Ctrl+Z/Y/X/C/V/A, call **neither** `onKeyDown(e)` nor `handleKey(e)` (so HaxeUI's KEY_DOWN and Simplified's Enter/Escape `finish` never see them). Other keys behave as today.
- `interactive.onPush` while composing → `commitComposition()` first.
- `interactive.onFocus` → `hxd.Window.getInstance().startTextInput()` (in addition to the existing software-keyboard call). `interactive.onFocusLost` → `commitComposition()` **before** `onFocusLost(e)`; `onBlur` → `stopTextInput()` after `hideSoftwareKeyboard`.
- Candidate area, recomputed in `sync` while focused: caret line rectangle in local space `(−scrollX, cursorY, inputWidth (or textWidth), font.lineHeight)` and `cursor = cursorX − scrollX`; corners through `localToGlobal`, then `getScene().camera.cameraToScreen` (window pixels); `setTextInputArea` with the rounded rect and cursor (the Window de-dupes).

- [ ] **Step 1: Write the failing test** `TextInputImeTest.hx` — `hxd.App` (with `drive.heaps.OffScreen.install()` first), a font from `hxd.res.DefaultFont.get()` with a fake `fallback` that returns a glyph for anything ≥ 0x80 (as in phase 1's `FontFallbackTest`), and an `h2d.TextInput` at (50, 40) in `s2d`, width 200. Steps driven by `hxd.Window.getInstance().event(e)` like `PasswordMaskTest`. Checks:
  - focus → `hxd.Window.getInstance().textInputActive`; blur → `!textInputActive`.
  - text "ab", caret 1, composition "ni" cursor 2 → `text == "ab"`, `displayText() == "anib"`, `onChange` count 0, `cursorIndex == 3`.
  - ENTER, BACKSPACE, LEFT key-downs while composing → `onKeyDown` delegate count 0, `text == "ab"`, `onSubmit` count 0.
  - commit "你" (`ETextInput` 0x4F60) → `text == "a你b"`, `!isComposing()`, `cursorIndex == 2`, one `onChange`; Ctrl+Z → `"ab"`.
  - commit "你好" as two events in one frame → one undo step restores the previous text.
  - empty composition while not composing → no `onChange`, cursor unchanged (Review Focus 3).
  - selection "ab" all + composition "x" → selection gone, `text == ""`, `displayText() == "x"`.
  - composition "ka", then blur → `text` ends with "ka", `onFocusLost` delegate saw `text` already containing "ka".
  - after blur, a composition event → `text` unchanged (Review Focus 5).
  - fonts without fallback: commit 0x4F60 → dropped (unchanged behaviour); with fallback: kept.
  - single-line commit "a\nb" → "ab"; multi-line commit "a\r\nb" → "a\nb" (Review Focus 4).
  - multi-line field width 60, text filling one line, composition long enough to wrap → `getSplitLines().length == 2` while composing and `text` has no inserted line (Review Focus 1).
  - candidate area: caret at end of "ab" → `textInputArea.x == 50·scale`, `y == 40·scale`, `h == round(font.lineHeight·scale)`, `cursor == round(cursorX·scale)` with `s2d.scaleMode` producing a known scale (e.g. `Zoom(2)`); a long single-line text scrolled right → `cursor <= w` (Review Focus 2); a second `sync` with nothing moved sends nothing (spy via `textInputArea` identity).
  Exit 0 with `TextInputImeTest: all passed`.
- [ ] **Step 2: Run to verify it fails** — `cd D:\_github\Simplified; haxe test-heaps-hl.hxml` (or only the new block) → compile error (`isComposing` unknown).
- [ ] **Step 3: Implement** the behaviour above in `h2d/TextInput.hx`.
- [ ] **Step 4: Run to verify it passes** — the new block prints `TextInputImeTest: all passed`; every other block in `test-heaps-hl.hxml` still passes.
- [ ] **Step 5: Commit** — Heaps: `git add h2d/TextInput.hx && git commit -m "h2d.TextInput: inline IME composition, key gating, focus-driven text input, candidate area"`; Simplified: `git add tests-heaps/TextInputImeTest.hx test-heaps-hl.hxml && git commit -m "tests: h2d.TextInput IME behaviour"`

### Task 4: `h2d.TextInput` — surrogate pairs are never split

**Files:**
- Modify: `h2d/TextInput.hx`; Simplified `tests-heaps/TextInputImeTest.hx` (add cases)

**Interfaces:**
- Consumes: Task 3's `displayText`/cursor model.
- Produces: private `prevCursor(i:Int) : Int` and `nextCursor(i:Int) : Int` (one *character* back/forward in cursor space, stepping over a pair as one); used by LEFT/RIGHT, single-character BACKSPACE/DELETE, `textPos` (click never lands between halves), drag-selection ends, and word jumps' final clamp. `inputText(String.fromCharCode(cp))` for cp > 0xFFFF must insert a pair — assert it in the test; if HL's `fromCharCode` doesn't, build the pair by hand.

- [ ] **Step 1: Write the failing tests** — text `"a😀b"`, caret at end: BACKSPACE twice → `"a"`; caret 1 + DELETE → `"ab"`; caret 0, RIGHT → `cursorIndex == 1`, RIGHT → `3`; LEFT from 3 → 1; click at the x of the emoji's middle → `cursorIndex` is 1 or 3, never 2; commit 0x1F600 into `""` → `text.length == 2`. After every step: no index `i` where `text.charCodeAt(i)` is a high surrogate not followed by a low one.
- [ ] **Step 2: Run to verify it fails** — the new cases FAIL (lone surrogates).
- [ ] **Step 3: Implement** `prevCursor`/`nextCursor` and route the listed operations through them.
- [ ] **Step 4: Run to verify it passes** — `TextInputImeTest: all passed`.
- [ ] **Step 5: Commit** — Heaps `git commit -am "h2d.TextInput: never split surrogate pairs"`; Simplified `git commit -am "tests: surrogate-safe editing"`.

### Task 5: haxeui-heaps — caret, selection and password while composing

**Files:**
- Modify: `haxe/ui/backend/TextInputImpl.hx` (haxeui-heaps fork)
- Create (Simplified): `tests-heaps/ImeTextFieldTest.hx`; Modify: `test-heaps-hl.hxml` (new `--next` block like `PasswordMaskTest`'s)

**Interfaces:**
- Consumes: `h2d.TextInput.isComposing()`, `displayText()`, `composition`, `commitComposition()` (Task 3).
- Produces: while composing, `TextInputImpl`'s caret/selection getters (`get_caretIndex`, `get_selectionStartIndex`, `get_selectionEndIndex`) report the committed caret (`textInput.getTextPos` of the anchor, i.e. where the composition sits in `text`), setters first call `commitComposition()`; its scroll-to-caret and `TextSelection` drawing use the displayed caret and draw no selection; `ScrollingTextInput.initGlyphs` masks the **displayed** text in password mode (mask `displayText()` when its argument is the text, so `h2d.TextInput`'s splice can't add clear characters after masking).

- [ ] **Step 1: Write the failing test** `ImeTextFieldTest.hx` (HaxeUI `TextField`, width 300, events via `hxd.Window.event` as in `PasswordMaskTest`):
  - focus, type "ab", composition "nihao" → `field.text == "ab"`, field `onChange` count unchanged, `field.caretIndex == 2`.
  - a `KeyboardEvent.KEY_DOWN` listener registered on the field (as `IncidentTreeRowRenderer.startEditing` does) sees no Enter/Escape while composing; Enter after the commit is seen.
  - commit "你好" → `field.text == "ab你好"`.
  - a `FocusEvent.FOCUS_OUT` listener reading `field.text` mid-composition "ka" → reads `"…ka"` (the commit happened first).
  - password field: composition "iii" and "WWW" give the same text width (the `PasswordMaskTest` idea), and neither equals the width of the clear string.
  - a field narrower than its text with composition at the end → the field's scroll shows the caret (displayed caret x − scroll ≤ field width).
- [ ] **Step 2: Run to verify it fails** — the password and caret checks FAIL.
- [ ] **Step 3: Implement** the changes in `TextInputImpl.hx`.
- [ ] **Step 4: Run to verify it passes** — `ImeTextFieldTest: all passed`; `PasswordMaskTest` and the rest of `test-heaps-hl.hxml` still pass.
- [ ] **Step 5: Commit** — haxeui-heaps `git commit -am "TextInputImpl: caret, selection and password mask during IME composition"`; Simplified `git add tests-heaps/ImeTextFieldTest.hx test-heaps-hl.hxml && git commit -m "tests: HaxeUI text field IME behaviour"`.

### Task 6: Builds — Android `libhlsdl.so`, JoeClash and Simplified compile

**Files:**
- Modify (heaps-android): rebuilt `thirdparty/<abi>/libhlsdl.so` (via `tools/build-hashlink-android.bat`, which copies from hashlink-full)

- [ ] **Step 1: Android libs** — run `tools\build-hashlink-android.bat` in heaps-android → exit 0; `git status` shows `thirdparty/*/libhlsdl.so` changed (and possibly `libhl.so`; keep only files the script rebuilt from changed sources — revert unrelated churn).
- [ ] **Step 2: Simplified Android build** — `cd D:\_github\Simplified; haxe android.hxml` → APK built; `adb install -r` on the Pixel succeeds.
- [ ] **Step 3: Desktop builds** — Simplified `haxe heaps-hl.hxml` → builds; JoeClash: its HL JIT hxml builds, and `tools/build_exe.ps1 -Static` links (it compiles `sdl.c` in).
- [ ] **Step 4: Commit** — heaps-android `git add thirdparty && git commit -m "Refresh libhlsdl.so: IME text input"`.

### Task 7: Verification on devices (the spec's manual matrix)

Needs a person at the keyboard/phone (or computer-use for Windows). Run Simplified's desktop build (`hl build/heaps/hl/Main.hl`) and the Android APK. For each item, note pass/fail in the notes folder.

- [ ] **Windows, Microsoft Pinyin** — new incident → title: "nihao" shows underlined inline, candidate list under the caret; Space/1 picks 你好; long phrase; Escape mid-composition leaves nothing; click away mid-composition keeps the underlined text once; Enter to confirm does not save the row; Enter afterwards saves; title scrolled long → list still under the caret.
- [ ] **Windows, Microsoft Japanese IME** — kana → kanji with Space, re-conversion, Enter confirms without saving.
- [ ] **Windows, no field focused** — pressing letters with Pinyin active shows no composition box; app shortcuts work.
- [ ] **Windows, English** — typing, Ctrl+Z/Y/C/V/X/A, arrows, selection unchanged.
- [ ] **Android Pixel, Gboard** — English with predictions (tap a suggestion → whole word), Chinese Pinyin, Japanese, an emoji (stored intact, draws as a box).
- [ ] **Persistence** — save each incident, reopen: titles exact.
