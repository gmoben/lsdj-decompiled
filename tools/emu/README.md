# Dynamic analysis harness (SameBoy debugger)

Drive LSDj in **SameBoy** (GB-native debugger) to *verify* the static labels:
read/write live memory, breakpoints/watchpoints, button input, and screenshots.
The emulator runs **visibly** on the Xvnc display `:1` (connect a VNC viewer to
`localhost:5901` to watch).

## Why SameBoy
mGBA's GDB stub crashes on GB memory reads, and mGBA 0.10.5 can't autoload a Lua
script (`--script` is unsupported). SameBoy's text debugger is GB-accurate and
fully drivable over a pty. Input goes through `xdotool` (SDL needs a real
window-focus click first; keys are *held* so LSDj's once-per-frame joypad poll
catches them).

## Usage
```sh
# start (detached); spawns SameBoy on :1, serves .emu/emu.sock (gitignored, in-repo)
mkdir -p .emu
setsid -f python3 tools/emu/emud.py "$PWD/lsdj9_4_0.gb" </dev/null >.emu/emud.log 2>&1
sleep 8 && python3 tools/emu/emu.py CONT          # resume (LSDj boots, visible)

python3 tools/emu/emu.py read c401 1              # read N bytes  -> hex
python3 tools/emu/emu.py read wKeyDelay 2         # ...by NAME (resolved from constants/ram.inc)
python3 tools/emu/emu.py write wKeyDelay 00       # POKE byte(s) deterministically (name or hex addr)
python3 tools/emu/emu.py keyrepeat off            # named setter: disable key-repeat (see PARAMS)
python3 tools/emu/emu.py RAW 'x $c401'            # raw SameBoy debugger command
python3 tools/emu/emu.py RAW 'watch $c411'        # write-watchpoint (find writers)
python3 tools/emu/emu.py clearwatch               # remove ALL break/watchpoints (do this when done!)
python3 tools/emu/emu.py KEY 'select+right'       # input: names a/b/start/select/up/down/left/right (+ chords)
python3 tools/emu/emu.py SHOT .emu/s.png          # screenshot the framebuffer
python3 tools/emu/emu.py BREAK | CONT | STATE | WID | QUIT
```

## Deterministic memory writes
SameBoy has no `poke` command, but its expression evaluator writes memory via a
dereferenced assignment: **`print [$addr] = $val`** (one byte per expression).
`emu.py write`/`read` wrap this and are **symbol-aware** — the address may be a
bare hex value *or* a variable name from `constants/ram.inc` (e.g. `wKeyDelay`).

Higher-level **named setters** live in the `PARAMS` table in `emu.py`; extend it
as more useful live state is identified. Current entries:
- `keyrepeat on|off` — toggles `wKeyDelay` ($C52B, +working timer $C406). `off`
  (delay 0) makes held keys register **exactly once**, so scripted input is
  deterministic (no off-by-one from autorepeat). Run it once after boot.

## Verified keymap (this SameBoy build)
arrows = D-pad, `x`=A, `z`=B, `Return`=Start, `BackSpace`=Select.
`wButtons` ($C575) bit layout (dirs low / buttons high): Right0 Left1 Up2 Down3
A4 B5 Select6 Start7.

Screen navigation is `SELECT`+direction (chord, e.g. `KEY select+down`); the
lower-right nav map shows the current screen and which direction reaches each
neighbour. Verified: PROJECT `09` --down--> SONG `04` --right--> CHAIN `03`. The
first chord after a batch of non-`KEY` ops can miss (focus warmup) — re-send it.

## Notes
- `emud.py` keeps one persistent debugger session (SameBoy quits on debugger
  disconnect); stateless callers talk to it via the socket.
- Reads auto-`BREAK` then resume; the game keeps running between inspections.
- Watchpoints: `unwatch`/`delete` take a watchpoint **id** (see `list`), *not* an
  address. Clearing by address silently no-ops, leaving stale watchpoints that
  re-stop the game on every resume (looks frozen). Run `emu.py clearwatch` when
  done tracing to remove them all.
