# Dynamic analysis cookbook (driving LSDj in SameBoy)

How to interact with the emulated game to **verify** RAM-variable meanings
deterministically, instead of guessing. Companion to `README.md` (which covers
the harness itself). Everything here is verified against this ROM (9.4.0).

The golden rule: **change something in the game, then diff memory** — or poke a
candidate byte and watch the screen. Treat static analysis and any externally
suggested name as a *hypothesis*; confirm it against what the running ROM does.

---

## 1. Bring-up

```sh
# writable copy of the BUILT rom + its song save (so a real song autoloads),
# kept under the repo in the gitignored .emu/ dir (the daemon serves
# .emu/emu.sock there too) -- nothing is left in /tmp.
mkdir -p .emu
cp build/lsdj9_4_0.gb  .emu/lsdj_song.gb
cp build/lsdj9_4_0.sav .emu/lsdj_song.sav && chmod u+w .emu/lsdj_song.sav
setsid -f python3 tools/emu/emud.py "$PWD/.emu/lsdj_song.gb" </dev/null >.emu/emud.log 2>&1
sleep 8 && python3 tools/emu/emu.py CONT          # boot (visible on Xvnc :1 -> VNC localhost:5901)
sleep 11                                          # LSDj boots + autoloads the song
```
The top-level `lsdj9_4_0.gb` has NO useful song; the `build/` save does. Never
commit the ROM or `.sav` (the whole `.emu/` dir is gitignored).

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

## 7. Names from other-version disassemblies (verify, don't trust)

Public disassemblies of *other* LSDj versions can suggest WRAM names, but they're
for different builds and are wrong in places — treat each as a hypothesis only and
confirm it against our code and the running ROM. Recurring pitfalls:
- **Address drift**: a name can be off by a byte or two between versions. We've
  seen a consistent **+1** across the `$c4-$cb` block (the name for our address
  `A` sits at `A+1` there), but it does **not** hold in the `$c8xx` pointer
  cluster (laid out differently). Our init code is authoritative — read where
  each var is actually written.
- **Wrong meaning**: a label can contradict what our consumers actually do
  (e.g. `$c333` is the per-channel TRANSPOSE — our table code proves it).
- **Bit layouts** (instrument param bytes: byte5 = PITCH_SHAPE/TABLE_STEP/
  PITCH_SPEED/TRANSPOSE, byte7 = OUTPUT/pan, byte8 = COMMAND_RATE, byteB =
  FINETUNE) must be re-checked bit-for-bit against `SetupSoundPU1`'s masks.

Keep this repo self-contained: never cite or link an external disassembly in our
files, commits, or comments — fold only *verified* facts back in, in our terms.

---

## 8. Helper scripts (all in-repo)

- `tools/emu/song.py` — **editing driver**: a verified layer over `emu.py`/`nav.py`
  that drives the editors so features actually run (turning `_LABEL_`/`_DATA_`
  auto-labels into reverse-engineered code/tables). Every mutation is confirmed
  against the per-screen cursor RAM. Verified-live primitives: screen `goto`,
  `moveto(col,row)` (seeks + verifies the cursor var), `place`/`clear`/`inc`/`dec`
  (A / A+B / A+up / A+down), PHRASE `note(row,steps,instr)` (places a note + steps
  pitch), `fx(row,steps)` (places an FX command). CLI mirrors the API
  (`song.py note 5 4`, `song.py goto phrase`, `song.py moveto 1 3`). Findings from
  building it: PHRASE `wPhraseCol` is **1=NOTE 2=INSTR 3=CMD 4=CMD-value** (col 0
  unreachable — ram.inc was off by one). Note-edit keys (verified): **A+up/down =
  octave ±1, A+left/right = semitone ±1**; `place` (A) enters the last-used note,
  so `note()` reaches an exact pitch by octave+semitone steps from that default.
  SONG/CHAIN/INSTR/TABLE/SYNTH/WAVE primitives are being extended + verified.
- `tools/emu/diffstate.py <start> <len> [op...]` — label-aware diff-on-action
  (§4). The main verification tool. (Supersedes the old unlabelled region diff.)
- `tools/emu/snddump.py <start> <len>` — annotated snapshot of the sound block,
  4/row with the per-channel `[4]` array names (live RAM via `emu.py`).
- `tools/statevar.py <addr...>` — static: which functions write/read an address
  and with what values. Its `hl`-tracking is heuristic and can misattribute
  address arithmetic as a write, so verify the cited sites.
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
