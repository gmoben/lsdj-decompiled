# Dynamic analysis cookbook (driving LSDj in SameBoy)

How to interact with the emulated game to **verify** RAM-variable meanings
deterministically, instead of guessing. Companion to `README.md` (which covers
the harness itself). Everything here is verified against this ROM (9.4.0).

The golden rule: **change something in the game, then diff memory** — or poke a
candidate byte and watch the screen. Cross-check static analysis and the private
another disasm reference against what the running ROM actually does.

---

## 1. Bring-up

```sh
# writable copy of the BUILT rom + its song save (so a real song autoloads)
cp build/lsdj9_4_0.gb  /tmp/opencode/lsdj_song.gb
cp build/lsdj9_4_0.sav /tmp/opencode/lsdj_song.sav && chmod u+w /tmp/opencode/lsdj_song.sav
setsid -f python3 tools/emu/emud.py /tmp/opencode/lsdj_song.gb </dev/null >/tmp/opencode/emud.log 2>&1
sleep 8 && python3 tools/emu/emu.py CONT          # boot (visible on Xvnc :1 -> VNC localhost:5901)
sleep 11                                          # LSDj boots + autoloads the song
```
The top-level `lsdj9_4_0.gb` has NO useful song; the `build/` save does. Never
commit the ROM or `.sav`.

---

## 2. Input mechanics (verified)

- Keymap: arrows = D-pad, `x`=A, `z`=B, `Return`=Start, `BackSpace`=Select.
  `KEY` names: `a b start select up down left right` (+ `+` chords).
- LSDj polls the joypad once/frame, so the harness **holds** each key ~0.13 s
  (~8 frames). One held press normally = one action.
- **Key-repeat causes off-by-one** on held presses. Disable it for clean input:
  `python3 tools/emu/emu.py keyrepeat off`  (pokes `wKeyDelay`=0). Re-run after
  loading a different song (the song's saved KEY DELAY overrides it).
- **Chords** (e.g. `select+down`) press both keys together. The **first chord
  after a batch of non-`KEY` ops can miss** (SDL focus warmup) — just re-send it.
- Single-A toggles (e.g. PRELISTEN) sometimes don't register from a held press;
  prefer the write-and-redraw method (§5) for those.

---

## 3. Screen navigation

- `SELECT`+direction moves between screens. The lower-right **nav map** shows the
  current screen + which direction reaches each neighbour.
- Layout (verified): `PROJECT` above `SONG`; the main row is
  `SONG(4) CHAIN(3) PHRASE(1) INSTR(6) TABLE(5)` (left↔right); `GROOVE(2)` below;
  `SYNTH($0a)`, `WAVE(8)` under the instrument column; `FILE($0d)`, `HELP($0e)`.
- **Content-gated**: you can only enter CHAIN if a chain exists at the SONG
  cursor (`--` = empty won't move); likewise CHAIN→PHRASE needs a phrase. Create
  one with `A` (or `A+up`) on an empty cell first.
- `wCurrentScreen $C401` holds the id — always read it back to confirm a nav
  worked (chords are flaky). `B` backs out of HELP.
- Screen ids: SONG=4 CHAIN=3 PHRASE=1 GROOVE=2 TABLE=5 INSTR=6 WAVE=8 PROJECT=9
  SYNTH=$0a FILE=$0d HELP=$0e (see `constants/screens.inc`).

---

## 4. Verification technique A — diff on action (`diffstate.py`)

`tools/emu/diffstate.py <start-hex> <len> [emu-op args...]` snapshots a WRAM
region, performs an action, snapshots again, and prints changed bytes annotated
with their names from `ram.inc` (so unknowns stand out). One ranged read = one
paused moment, so a snapshot is internally consistent.

```sh
python3 tools/emu/diffstate.py c400 256 KEY right     # which byte is the cursor column?
python3 tools/emu/diffstate.py c520 48 KEY a+up       # which byte does this setting edit?
```
Recipes:
- **Cursor**: move UP/DOWN/LEFT/RIGHT on an editor; the byte that ±1s is the
  cursor row/col. (SONG col=`wSongCol`, row=`wSongRow`; CHAIN `wChainCol/Row`;
  GROOVE `wGrooveRow`; etc.)
- **Setting value**: put the cursor on the line, `A+up`/`A+down`/`A+left/right`,
  diff. e.g. TEMPO edit → `wTempo $C529` changed 124→134.
- **Ignore the input-flag noise** that shows on every press: `wButtonsSame
  $C408`, `wAHeld/wSelectHeld $C40A/C`, `wButtonReadIdx/WriteIdx $C56F/70`.
- **Stop playback first** (`KEY start` on SONG) so the sound engine doesn't add
  noise to UI diffs.

Caveat: **per-song/global settings live in SRAM** ($A000-$BFFF, banked). Editing
TRANSPOSE/SYNC/etc. writes SRAM, and the WRAM working copy may not refresh until
the screen/song reloads — so a WRAM diff can show nothing. Use §5 instead, or
diff SRAM with the right bank selected.

---

## 5. Verification technique B — write-and-redraw (best for settings)

LSDj only redraws a value tile when it changes via *input*, so a raw poke leaves
the on-screen number stale. Exploit that:

1. `emu.py write <var> <hex>` to poke a candidate byte.
2. Force a full redraw: navigate away and back (`SELECT+down` then `SELECT+up`),
   or change screen — the screen re-reads the byte on entry.
3. Screenshot: if the displayed setting changed to match your poke, the byte IS
   that setting.

Verified `wPrelisten $C56E` this way: poked 0 → PROJECT redrew "PRELISTEN OFF".

---

## 6. Verification technique C — watchpoints (find the writer)

```sh
emu.py RAW 'watch $c0f3'   # write-watchpoint (default is write-only)
emu.py CONT ; sleep 1 ; emu.py DRAIN 2   # capture the hit: shows the writing PC
emu.py clearwatch          # ALWAYS clear when done (see gotcha)
```
The hit prints the PC just *after* the write; read the disasm there and walk back
to the producer/consumer. `watch/r` and `watch/w` set read/write; conditional:
`watch $c205 if ($c205 != $ff)`.

**Memory writes**: SameBoy has no `poke`; its evaluator does it via
`print [$addr] = $val` (1 byte). `emu.py write` wraps this (symbol-aware).

---

## 7. Cross-referencing the another disasm reference (verify, don't trust)

another disasm (`/tmp/opencode/another disasm`, a another-version disassembly) names ~291 WRAM syms —
a great **hypothesis source**, but it is for a *different version* and is wrong in
places. Always confirm against our code/emulator. Two pitfalls found:
- **Layout shift**: our `$c0-$c3` matches another disasm 1:1, but `$c4-$cb` is **+1**
  (another disasm name for our addr `A` is at `A+1`). Verified on `wKeyDelayTimer`,
  `wSongRow`, `wInstrField`, `wColorSet`, `wButtons`, `wTempo`, `$c900`.
- The shift does **not** hold everywhere in `$c8xx` (the current-channel pointer
  cluster is laid out differently between versions). Our init code is
  authoritative there — read it.
- It also *mislabels* things our consumers contradict (e.g. it calls `$cbbc`
  ENV_STAGE_1 — correct; but its byte-meaning notes must be checked: `$c333` is
  TRANSPOSE, our code proves it).
- Use its **instrument param constants** (byte layout: byte5 = PITCH_SHAPE/
  TABLE_STEP/PITCH_SPEED/TRANSPOSE, byte7 = OUTPUT/pan, byte8 = COMMAND_RATE,
  byteB = FINETUNE) — but verify each bit against `SetupSoundPU1`'s masks.

NEVER reference the another disasm repo in our repo's files/commits/comments.

---

## 8. Helper scripts

- `tools/emu/diffstate.py <start> <len> [op...]` — label-aware diff-on-action
  (§4). The main verification tool; lives in the repo.
- In `/tmp/opencode` (ephemeral; recreate if needed): `findvar.py` (older region
  diff, no labels), `snddump.py` (annotated `[4]` sound snapshot), `statevar.py
  <addr...>` (static: who writes/reads an addr + values — note its `hl`-tracking
  can misattribute address-arithmetic as writes, so verify the sites).
- `emu.py` ops: `read/write <addr|name> ...`, `keyrepeat on|off`, `clearwatch`,
  `RAW <cmd>`, `BREAK/CONT/STATE/DRAIN/SHOT/KEY/WID/QUIT`. `read`/`write` resolve
  names from `ram.inc`.

---

## 9. Gotchas

- `pkill`/`pgrep` **hang** on this box — use `ps -eo pid,comm | awk` + `kill -9`.
- `unwatch`/`delete` take a watchpoint **id**, not an address; clearing by
  address silently no-ops → stale watchpoints re-stop the game every resume and
  it looks frozen. Use `emu.py clearwatch`.
- The daemon's running-flag can desync if a watchpoint fires async — `DRAIN` to
  resync, or `clearwatch`.
- On-screen value tiles are **stale** after a poke until a redraw (this is a
  feature — see §5).
- Reads pause the game (`BREAK`) then resume; the game keeps running between
  inspections. Separate `read` calls are different moments — use one ranged read
  for a consistent snapshot.
- A live song advances the sequencer/sound state constantly; stop playback for
  quiet UI diffs.
