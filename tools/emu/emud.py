#!/usr/bin/env python3
"""Persistent SameBoy debugger daemon for dynamic LSDj analysis.

Spawns SameBoy (visible on DISPLAY=:0) under a pty, holds its text debugger,
and serves a line protocol over a Unix socket so stateless callers can:
  RAW <cmd>     - send a raw SameBoy debugger command, return output to prompt
  BREAK         - Ctrl-C into the debugger (pause the running game)
  CONT          - resume the game (no prompt until next break)
  STATE         - 'stopped' or 'running'
  SHOT <path>   - capture the SameBoy window to a PNG (via import)
  KEY <keys>    - send key(s) to the SameBoy window (xdotool), space-separated
  WID           - report the X window id
  QUIT          - kill SameBoy and exit the daemon
Usage: emud.py /abs/path/rom.gb   (run detached; talk to /tmp/opencode/emu.sock)
"""
import os, sys, pty, select, time, socket, subprocess, signal

SOCK = "/tmp/opencode/emu.sock"
DISPLAY = os.environ.get("EMU_DISPLAY", ":1")  # :1 = Xvnc (watch via VNC :5901)

class Emu:
    def __init__(self, rom):
        self.master, slave = pty.openpty()
        # full env with DISPLAY overridden (X tools need XAUTHORITY/HOME to auth to :1)
        self.xenv = dict(os.environ); self.xenv["DISPLAY"] = DISPLAY
        self.xenv.pop("WAYLAND_DISPLAY", None)
        env = dict(self.xenv)
        env["SDL_AUDIODRIVER"] = "dummy"
        env["SDL_VIDEODRIVER"] = "x11"        # force X11 (the Xvnc :1), not Wayland
        self.proc = subprocess.Popen(
            ["sameboy", "-s", rom], stdin=slave, stdout=slave, stderr=slave,
            env=env, start_new_session=True)
        os.close(slave)
        self.running = False
        time.sleep(2.0)
        self._read(2.0)              # consume banner + first prompt
        self.wid = self._find_window()

    def _read(self, t=1.5, until_prompt=False):
        out = b""; end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.1)
            if r:
                try: d = os.read(self.master, 65536)
                except OSError: break
                if d:
                    out += d; end = time.time() + 0.3
                    if until_prompt and out.rstrip().endswith(b">"):
                        break
        return out.decode("utf-8", "replace")

    def raw(self, cmd):
        os.write(self.master, (cmd + "\n").encode())
        return self._read(3.0, until_prompt=True)

    def brk(self):
        if not self.running:
            return "(already stopped)"
        os.write(self.master, b"\x03")
        self.running = False
        return self._read(2.0, until_prompt=True)

    def cont(self):
        os.write(self.master, b"continue\n")
        self.running = True
        time.sleep(0.1)
        return "running"

    def _find_window(self):
        for _ in range(10):
            try:
                out = subprocess.run(["xdotool", "search", "--name", "SameBoy"],
                                     env=self.xenv, capture_output=True, text=True, timeout=5)
                ids = [x for x in out.stdout.split() if x]
                if ids: return ids[-1]
            except Exception: pass
            time.sleep(0.5)
        return ""

    def shot(self, path):
        if not self.wid: self.wid = self._find_window()
        if not self.wid: return "no-window"
        r = subprocess.run(["import", "-window", self.wid, path],
                           env=self.xenv, capture_output=True, text=True, timeout=10)
        return "ok" if r.returncode == 0 else "err:" + r.stderr.strip()

    # GB button name -> SameBoy default keysym (arrows confirmed; others default)
    KEYMAP = {"a": "x", "b": "z", "start": "Return", "select": "BackSpace",
              "up": "Up", "down": "Down", "left": "Left", "right": "Right"}

    def _focus(self):
        if not self.wid: self.wid = self._find_window()
        if not self.wid: return False
        subprocess.run(["xdotool", "windowactivate", "--sync", self.wid],
                       env=self.xenv, capture_output=True, timeout=5)
        geo = subprocess.run(["xdotool", "getwindowgeometry", "--shell", self.wid],
                             env=self.xenv, capture_output=True, text=True, timeout=5)
        w = h = 0
        for line in geo.stdout.splitlines():
            if line.startswith("WIDTH="): w = int(line.split("=")[1])
            if line.startswith("HEIGHT="): h = int(line.split("=")[1])
        if w and h:  # click center: SDL needs a real FocusIn to accept XTEST keys
            subprocess.run(["xdotool", "mousemove", "--window", self.wid, str(w//2), str(h//2), "click", "1"],
                           env=self.xenv, capture_output=True, timeout=5)
        return True

    def key(self, keys, hold=0.13):
        # space-separated tokens; '+' joins a held chord (e.g. select+right).
        # Each token's keys are pressed together and HELD ~hold s so LSDj's
        # once-per-frame joypad poll reliably catches them. Names or raw keysyms.
        if not self._focus(): return "no-window"
        for tok in keys.split():
            syms = [(self.KEYMAP.get(p.lower()) or p) for p in tok.split("+")]
            for ks in syms:
                subprocess.run(["xdotool", "keydown", ks], env=self.xenv, capture_output=True, timeout=5)
            time.sleep(hold)
            for ks in reversed(syms):
                subprocess.run(["xdotool", "keyup", ks], env=self.xenv, capture_output=True, timeout=5)
            time.sleep(0.06)
        return "ok"

def serve(rom):
    if os.path.exists(SOCK): os.unlink(SOCK)
    emu = Emu(rom)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK); srv.listen(4)
    while True:
        conn, _ = srv.accept()
        try:
            data = conn.recv(65536).decode("utf-8", "replace").strip()
            if not data: conn.close(); continue
            op, _, arg = data.partition(" ")
            op = op.upper()
            if op == "RAW":    reply = emu.raw(arg)
            elif op == "BREAK":reply = emu.brk()
            elif op == "CONT": reply = emu.cont()
            elif op == "STATE":reply = "running" if emu.running else "stopped"
            elif op == "SHOT": reply = emu.shot(arg)
            elif op == "KEY":  reply = emu.key(arg)
            elif op == "WID":  reply = emu.wid or "(none)"
            elif op == "QUIT":
                try: os.killpg(os.getpgid(emu.proc.pid), signal.SIGKILL)
                except Exception: pass
                conn.sendall(b"bye\n"); conn.close(); break
            else: reply = "unknown op: " + op
            conn.sendall((reply + "\n").encode())
        except Exception as e:
            try: conn.sendall(("ERR " + repr(e)).encode())
            except Exception: pass
        finally:
            conn.close()
    srv.close()
    if os.path.exists(SOCK): os.unlink(SOCK)

if __name__ == "__main__":
    serve(sys.argv[1])
