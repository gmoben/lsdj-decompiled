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
# start (detached); spawns SameBoy on :1, serves /tmp/opencode/emu.sock
setsid -f python3 tools/emu/emud.py "$PWD/lsdj9_4_0.gb" </dev/null >/tmp/opencode/emud.log 2>&1
sleep 8 && python3 tools/emu/emu.py CONT          # resume (LSDj boots, visible)

python3 tools/emu/emu.py read c401 1              # read N bytes  -> hex
python3 tools/emu/emu.py RAW 'x $c401'            # raw SameBoy debugger command
python3 tools/emu/emu.py RAW 'watch $c411'        # write-watchpoint (find writers)
python3 tools/emu/emu.py KEY 'select+right'       # input: names a/b/start/select/up/down/left/right (+ chords)
python3 tools/emu/emu.py SHOT /tmp/s.png          # screenshot the framebuffer
python3 tools/emu/emu.py BREAK | CONT | STATE | WID | QUIT
```

## Verified keymap (this SameBoy build)
arrows = D-pad, `x`=A, `z`=B, `Return`=Start, `BackSpace`=Select.
`wButtons` ($C575) bit layout (dirs low / buttons high): Right0 Left1 Up2 Down3
A4 B5 Select6 Start7.

## Notes
- `emud.py` keeps one persistent debugger session (SameBoy quits on debugger
  disconnect); stateless callers talk to it via the socket.
- Reads auto-`BREAK` then resume; the game keeps running between inspections.
