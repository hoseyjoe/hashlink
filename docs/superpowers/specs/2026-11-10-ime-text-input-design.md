# IME text input for Heaps on HashLink (system font fallback, phase 2) — design

Date: 2026-11-10. Status: draft for review.
Follows `2026-11-10-system-font-fallback-design.md` (phase 1: drawing any character from an OS font).

## Why

Phase 1 made any character *draw*. People still can't *type* most of the world's languages into a
Heaps app on HashLink. Chinese, Japanese and Korean are typed through an **IME**: you type pinyin or
kana, and the IME offers candidate words to pick. Today in a Heaps-on-HashLink app:

- **Windows:** the IME's own floating box appears in a random spot, not next to the cursor (nothing
  calls `SDL_SetTextInputArea`). The picked word arrives one `WM_CHAR` per character
  (`SDL_windowsevents.c` ~1639), so it isn't cut today — but `h2d.TextInput.handleKey` drops any
  character the font hasn't *already* cached (`if( !font.hasChar(e.charCode) ) return;`,
  `h2d/TextInput.hx:314`). `hasChar` never asks phase 1's fallback, so a Chinese character not yet
  drawn somewhere is silently thrown away.
- **Both:** `libs/sdl/sdl.c` packs committed UTF-8 text into the 4-byte `keyCode`, so any
  multi-character text event is cut to its first character. That bites on Android now, and on
  Windows as soon as the app draws composition (below), because SDL then sends the IME result as one
  event.
- **Android (Gboard):** SDL's Java `SDLInputConnection.updateText()` types the keyboard's composing
  text straight into the field (backspacing and retyping as it changes) and then sends the commit as
  one text event. A multi-character commit ("你好", a predicted English word, a pasted suggestion)
  is cut to its first character by the same `sdl.c` packing.

**The final test:** in Simplified (`D:\_github\Simplified`), create a new incident and type its title
in Chinese (Microsoft Pinyin) and Japanese (Microsoft IME) on Windows, and with Gboard on the attached
Android Pixel. The title must come out exactly as typed and stay that way after saving.

## What it should feel like (Joe chose this: "do your suggestion")

Like Chrome, Word or Notepad:

- **Windows:** the pinyin/kana being composed shows *inside the field*, at the caret, underlined, and
  the rest of the text moves aside for it. The IME's candidate list (drawn by Windows) opens just
  below the caret. Picking a word replaces the underlined text with the whole word. Escape cancels the
  composition (the IME handles that). Clicking elsewhere or tabbing away ends it cleanly.
- **Android:** looks the same as today — Gboard shows candidates in its own strip, and SDL types the
  composing word into the field as you go. The fix there is that whole words arrive whole.
- **Every text field** (HaxeUI fields and plain `h2d.TextInput`, so JoeClash too): text input / the
  IME switches **on** when a field gets focus and **off** when it loses it.

## Goals

1. **Commits arrive whole and are kept.** Any committed text, of any length and any Unicode code
   points, reaches `h2d.TextInput` and is inserted exactly. This is the fix that matters most, on
   every platform. It includes the `hasChar` drop: for a font with a phase-1 fallback installed, every
   character is accepted (one no OS font has draws as the font's box, phase 1 Goal 7). For a font with
   no fallback the check stays as it is, so nothing changes for apps that never installed one.
   *(My call.)*
2. **Inline composition on desktop.** Where the OS sends composition (Windows; also macOS and Linux
   through SDL), the composing text is drawn in the focused field at the caret with an underline,
   with SDL's composition cursor shown inside it. It is display-only: it is **not** part of the
   field's value, does not fire `onChange`, and never enters undo history.
3. **Candidate window at the caret.** While a field has focus, SDL is told where it is
   (`SDL_SetTextInputArea`: the rectangle of the **caret's line** in window coordinates — the field's
   width, one line high — plus the caret's x offset within it), and kept up to date as the caret moves, the text scrolls, or the field moves.
4. **Text input follows focus.** Text input is started for the window when a text field gains focus
   and stopped when no text field has focus. On Android this is the existing
   `showSoftwareKeyboard`/`hideSoftwareKeyboard` → `haxeui_show_keyboard` path, unchanged.
5. **Keys don't fight the IME.** While a composition is showing, the field ignores editing and
   navigation keys (Backspace, Delete, arrows, Home/End, Enter, Tab, Escape, Ctrl+Z/Y/X/C/V/A):
   those belong to the IME. In particular the Enter that confirms a pinyin word must **not** submit
   or blur a single-line HaxeUI field. On Windows SDL already swallows the keys the IME handles
   (`VK_PROCESSKEY`, `SDL_windowskeyboard.c` ~1093), so this is a backstop for other platforms and
   IMEs; it is proved by the automated test, not the manual one.
6. **Characters outside the BMP stay intact.** Emoji and CJK Extension B characters are stored as
   UTF-16 surrogate pairs in HashLink strings. Typing, cursor movement, Backspace/Delete, selection
   and composition never split a pair, so the value never holds a lone surrogate. (They still *draw*
   as boxes — see non-goals.)
7. **Platforms.** Bindings and Heaps changes compile for Windows, macOS, Linux, Android, iOS.
   **Tested** on Windows and Android only, as in phase 1.
8. **Tests** at each layer that can run without a person typing (see Testing).

## Non-goals

- **Drawing characters outside the BMP.** `h2d.Text` walks UTF-16 units, so an emoji draws as two
  boxes (and Android's colour emoji font has no outlines for SDF anyway). Fixing `h2d.Text` to walk
  code points is a separate follow-up.
- **App-drawn candidate lists** (`SDL_EVENT_TEXT_EDITING_CANDIDATES`). The OS draws them.
- **Inline composition on Android.** SDL's Java side never sends composition there; we don't change
  SDL's Java code. Gboard's composing word appears as ordinary typed text, as today.
- **Moving fields above the Android keyboard** when it covers them. Existing behaviour, not changed.
- Shaping, RTL/bidi (as phase 1). Testing macOS, Linux, iOS.
- IME *mode* control (forcing an IME on/off, input scopes like "numbers only", password fields
  turning the IME off). Later, if needed.

## Anti-goals (a failure even if it works)

- Pressing Enter to confirm an IME word submits the form or leaves the field.
- Composing text leaking into the saved value, `onChange`, or undo — or a cancelled composition
  leaving characters behind.
- Any change to how Latin typing behaves on a normal keyboard (keys, shortcuts, undo, selection).
- A lone surrogate ever ending up in a field's text.
- The candidate window opening at the top-left of the window, or somewhere other than the caret.
- Games' keyboard shortcuts being swallowed by an IME when no text field is focused.

## Design

### Layers

```
Simplified / JoeClash        — no change expected beyond rebuilding (and new libs on Android)
haxeui-heaps TextInputImpl   — keep its own caret/selection/scroll logic correct while a composition shows
h2d.TextInput                — draws composition, ignores keys while composing, start/stop + input area
h2d.Interactive/SceneEvents  — route the new composition event to the focused Interactive
hxd.Window (hl) / hxd.Event  — decode SDL events: whole commits, composition; text-input + area API
hlsdl (libs/sdl/sdl.c + Haxe) — pass commit and composition text as strings; start/stop/area per window
SDL3 3.4.x                   — unchanged
```

### hlsdl (`libs/sdl`, this repo)

- **Commit text whole.** `SDL_EVENT_TEXT_INPUT` copies `e.text.text` (UTF-8) into GC memory with
  `hl_copy_bytes` — the same way `SDL_EVENT_DROP_FILE` fills `dropFile` — and exposes it on
  `sdl.Event` as a new `text : hl.Bytes` field. `keyCode` keeps its old packed value for anything that
  still reads it.
- **Composition.** `SDL_EVENT_TEXT_EDITING` becomes a new event type `TextEditing` (next free value in
  both `event_type` and `sdl.EventType`), with the composing UTF-8 text in `text` (empty = composition
  ended/cancelled) and SDL's `start`/`length` (the composition cursor and selected span) in two
  existing or new int fields. Their unit depends on the platform — UTF-16 units on Windows
  (`GCS_CURSORPOS`), and they can be `-1` ("not set", e.g. ibus/fcitx) — so they are passed through
  raw and Heaps converts them (Windows: already UTF-16; elsewhere: code points; `-1` → caret at the
  end, no selected span). Adding fields to `event_data` must be mirrored exactly in
  `sdl/Event.hx` (field order = struct order).
- **Telling SDL we draw composition is Heaps' job, not hlsdl's:** other hlsdl apps keep the OS box.
  Heaps sets `SDL_HINT_IME_IMPLEMENTED_UI` = `"composition"` through the existing
  `sdl.Sdl.setHint` before it creates its window (SDL reads the hint at the first
  `SDL_StartTextInput`, which `win_create_ex` triggers). Without it, Windows keeps drawing its own
  box. Candidates stay with the OS.
- **Per-window text input control**, new prims plus `sdl.Window` methods:
  `startTextInput()`, `stopTextInput()`, `isTextInputActive()`, and
  `setTextInputArea(x, y, w, h, cursor)` (window coordinates; `cursor` is the caret's x offset inside
  the rectangle, as `SDL_SetTextInputArea` takes it). The existing C prim `text_input(bool)` (no
  Haxe binding today) is left alone.
- **Startup:** `win_create_ex` currently calls `SDL_StartTextInput(win)` for every new window
  ("SDL2 compat"). Leave that in hlsdl (other hlsdl users rely on it); Heaps stops it itself (below).

### Heaps (fork `C:/HaxeToolkit/haxe/lib/heaps/joe`, `master`)

- **`hxd.Event`:** new `EventKind` value `ETextComposition`, and new fields `composition : String`,
  `compositionCursor : Int`, `compositionLength : Int` (UTF-16 indices into `composition`; converted
  from SDL's code-point counts). Appended so nothing existing renumbers.
- **`hxd.Window` (hl, the `hlsdl` branch):**
  - `TextInput`: decode `e.text` as UTF-8 and dispatch **one `ETextInput` per code point**, with
    `charCode` = the code point. Every existing `onTextInput` consumer keeps working unchanged, and
    `h2d.TextInput`'s undo already merges changes less than 1 s apart, so a commit is one undo step.
  - `TextEditing`: dispatch one `ETextComposition`.
  - New API: `startTextInput()`, `stopTextInput()`, `setTextInputArea(x, y, w, h, cursor)` taking
    **scene-independent window coordinates**. On non-hlsdl targets these are no-ops.
  - At window creation Heaps calls `stopTextInput()`, so the IME is off until a text field (or the app)
    turns it on. *(My call — it gives Goal 4 and the "shortcuts not swallowed" anti-goal. Apps that read
    `ETextInput` with no focused `h2d.TextInput` must now call `startTextInput()` themselves; JoeClash,
    Simplified and EpubReader have no such code, checked by grep.)*
- **`hxd.SceneEvents` / `h2d.Interactive`:** route `ETextComposition` to the focused Interactive like
  `ETextInput`, and add `dynamic function onTextComposition(e)`.
- **`h2d.TextInput`:**
  - Holds the current composition (string, cursor, length). On `ETextComposition` it stores it,
    replacing any selection the user had (the selection is cut when the composition *starts*, as
    typing a character would), and redraws.
  - **Drawing:** the text is laid out as if the composition were inserted at the caret — so wrapping,
    line height and the HaxeUI scrolling window all account for it — with a 1 px underline under the
    composition (thicker under the IME's selected span if `compositionLength > 0`) and the caret at
    `compositionCursor`. The field's `text` value never contains it. *How* to get there (splicing in
    `initGlyphs`, a second `h2d.Text` overlay, …) is the builder's choice, provided the value stays
    clean and the caret/selection maths still match what's on screen.
  - **Keys:** while a composition is non-empty, `handleKey` ignores the keys listed in Goal 5.
  - **Commit:** `ETextInput` while composing clears the composition first, then inserts as today.
  - **Focus:** on focus → `startTextInput()` and set the input area; on blur → if a composition is
    showing, insert it as committed text (Chrome's behaviour; *my call*), then **always**
    `stopTextInput()`, even when focus moves straight to another text field (which then starts it
    again). Stopping is what makes the IME drop its copy of the composition
    (`SDL_StopTextInput` → `IME_ClearComposition`); without it the same text would be committed a
    second time in the next field.
  - **Input area:** recomputed after any change to caret position, text, scroll or the field's
    absolute position/scale, and sent only when it changed. Scene → window coordinates go through the
    scene's scale/offset (`h2d.Scene` scale modes) and the window's pixel-vs-point ratio.
  - **Surrogates (Goal 6):** cursor moves, Backspace/Delete, word jumps and click-to-place treat a
    surrogate pair as one character.
  - `showSoftwareKeyboard` / `hideSoftwareKeyboard` stay as they are (heaps-android replaces them).

### haxeui-heaps (`C:/HaxeToolkit/haxe/lib/haxeui-heaps/joe`)

`TextInputImpl` wraps `ScrollingTextInput` (a subclass of `h2d.TextInput`) and keeps its own caret,
selection-highlight (`TextSelection`) and scroll-to-caret logic based on `cursorIndex`. While a
composition shows, those must use the *displayed* caret (inside the composition), so the field
scrolls to keep the composition visible and the selection highlight isn't drawn over it. HaxeUI's
`onChange`/value must not see composing text (follows from `h2d.TextInput`). Enter-to-submit for
single-line fields must not fire while composing (follows from Goal 5). **Password fields** mask by
overriding `initGlyphs` (`TextInputImpl.hx` ~707): the composition is masked with the same bullet as
the rest, never shown in clear. *(My call: the IME can't be turned off without stopping text input
entirely, which would stop typing too.)*

### heaps-android (`C:/HaxeToolkit/haxe/lib/heaps-android/git`)

No logic change expected: SDL never sends composition there and the keyboard show/hide hooks stay.
It ships prebuilt `.so` files, so `libhlsdl.so` must be rebuilt from the new `sdl.c` and refreshed
(the same route phase 1 used for `libhlfontsys.so`).

### Apps

Simplified and JoeClash need only a rebuild against the new hlsdl/Heaps/haxeui-heaps (and a new
Android build of Simplified). JoeClash's HL/C static exe compiles `sdl.c` in, so it must be relinked.

## Testing

- **hlsdl, automated:** a small HashLink test that inits SDL (video), pushes synthetic
  `SDL_EVENT_TEXT_INPUT` ("你好世界", an emoji, ASCII) and `SDL_EVENT_TEXT_EDITING` events with
  `SDL_PushEvent`, and checks what `sdl.Event` reports. (A test-only prim to push them is fine.)
- **Heaps, automated (no GPU, like phase 1's `tests/font`):**
  - Characters the font hasn't cached yet are inserted when a fallback is installed (CJK, emoji),
    and still dropped when none is.
  - `hxd.Window` decoding: a fake `sdl.Event` with multi-byte UTF-8 → the right `ETextInput` code
    points; `TextEditing` → `ETextComposition` with correct UTF-16 cursor/length.
  - `h2d.TextInput`: composition shows but `text`/`onChange`/undo don't change; commit replaces it
    and is one undo step; Enter/Backspace/arrows ignored while composing and work again after;
    composing over a selection; blur commits; surrogate pairs never split by Backspace, Delete,
    arrows or selection.
  - Input area: a field at a known position in a scaled scene → expected window rectangle and caret
    offset; no resend when nothing moved.
- **Manual, Windows (Simplified `heaps-hl.hxml` build):** new incident → title field:
  Microsoft Pinyin ("nihao" → 你好, a long phrase, Escape mid-composition, click away mid-composition,
  Enter to confirm doesn't submit); Microsoft Japanese IME (kana → kanji conversion, re-conversion
  with Space, Enter); English typing and shortcuts unchanged; candidate window sits under the caret,
  also after scrolling a long title; IME off (no composition box) when no field is focused. Save and
  reopen the incident: the title is exact.
- **Manual, Android Pixel (Simplified `android.hxml`):** Gboard English with predictions (tap a
  suggestion → whole word), Chinese Pinyin, Japanese, an emoji (stored intact, draws as a box);
  save and reopen.

## Constraints

- SDL3 3.4.x as already vendored; don't patch SDL itself (including its Android Java).
- Same repos and branches as phase 1: hashlink-full `master`, Heaps fork `master`, haxeui-heaps fork,
  heaps-android, Simplified, JoeClash. One commit per logical change, nothing pushed without Joe's OK.
- HashLink JIT and HL/C both must work.

## Left to the builder

- Exact names of new prims/fields, as long as `sdl.c` and `sdl/Event.hx` stay in lockstep.
- How `h2d.TextInput` draws the composition (see above), underline style details.
- How a test pushes synthetic SDL events.
- Order of work across repos.

## Calls I made (for Joe to check)

- Inline composition drawn by the app on desktop, candidate list drawn by the OS.
- One `ETextInput` per code point (keeps every existing consumer working) rather than one event
  carrying a whole string.
- Heaps turns text input **off** at startup and on only while a text field has focus.
- Blur mid-composition **commits** what's underlined (Chrome) rather than discarding it; text input
  is always stopped and restarted between fields so the IME can't commit it twice.
- `h2d.TextInput` accepts any character when the font has a fallback (today it drops anything not
  already cached); fonts without a fallback behave as before.
- The "we draw composition" SDL hint is set by Heaps, not hlsdl, so other hlsdl apps don't change.
- Password fields show the composition as bullets.
- Emoji / Extension B characters: kept intact when editing, but still drawn as boxes (follow-up).
- Android keeps SDL's "type the composing word live" behaviour; no inline underline there.
