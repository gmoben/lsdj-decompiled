#!/usr/bin/env python3
r"""Thin client for the SameBoy debugger daemon (emud.py).
Usage: emu.py <OP> [args...]
  emu.py RAW "x $c401"       emu.py BREAK      emu.py CONT
  emu.py STATE               emu.py SHOT /tmp/s.png
  emu.py KEY Return          emu.py WID        emu.py QUIT
Higher-level helpers:
  emu.py read <hexaddr> <len>   -> break, x, parse bytes, (resume if was running)
"""
import socket, sys, re, time
SOCK = "/tmp/opencode/emu.sock"

def call(msg, t=15):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(t)
    s.connect(SOCK); s.sendall(msg.encode())
    out = b""
    while True:
        try: d = s.recv(65536)
        except socket.timeout: break
        if not d: break
        out += d
    s.close(); return out.decode("utf-8", "replace")

def read_mem(addr, length):
    """Break (if running), read `length` bytes from addr via repeated `x`, resume."""
    was = call("STATE").strip() == "running"
    if was: call("BREAK")
    a = int(addr, 16) if isinstance(addr, str) else addr
    vals = []
    off = 0
    while off < length:
        out = call(f"RAW x ${a+off:04x}")
        # SameBoy prints lines like: "c401: d1 b7 5f ef ..."  (16 bytes/line)
        for line in out.splitlines():
            m = re.match(r"\s*([0-9a-fA-F]{4}):\s+((?:[0-9a-fA-F]{2}\s+)+)", line)
            if m:
                vals += [int(x, 16) for x in m.group(2).split()]
                break
        off += 16
    vals = vals[:length]
    if was: call("CONT")
    return vals

if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "read":
        addr, length = sys.argv[2], int(sys.argv[3])
        vals = read_mem(addr, length)
        print(" ".join(f"{v:02x}" for v in vals))
    else:
        print(call(" ".join(sys.argv[1:])), end="")
