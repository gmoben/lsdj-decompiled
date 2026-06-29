#!/usr/bin/env python3
r"""Thin client for the SameBoy debugger daemon (emud.py).
Usage: emu.py <OP> [args...]
  emu.py RAW "x $c401"       emu.py BREAK      emu.py CONT
  emu.py STATE               emu.py SHOT .emu/s.png
  emu.py KEY Return          emu.py WID        emu.py QUIT
Higher-level helpers (addr may be a hex address OR a name from constants/ram.inc):
  emu.py read  <addr> <len>          -> break, x, parse bytes, (resume if was running)
  emu.py write <addr> <hexbyte>...   -> break, poke byte(s), (resume if was running)
        deterministic memory write via SameBoy `print [$addr] = $val` (1 byte/expr)
  emu.py keyrepeat <on|off>          -> deterministic key-repeat toggle (wKeyDelay)
  emu.py clearwatch                  -> remove ALL breakpoints/watchpoints, resume
        (SameBoy's unwatch/delete take an *id*, not an address; clearing by
        address silently no-ops and leaves stale watchpoints that re-stop the
        game on every resume -- looks "stuck". clearwatch when done with traces.)
Named deterministic setters live in PARAMS (extend as more state is identified).
"""
import socket, sys, re, time, os
# Control socket in the gitignored .emu/ dir under the repo (matches emud.py);
# override with EMU_RUNDIR. Keeps runtime state in-repo instead of /tmp.
RUNDIR = os.environ.get("EMU_RUNDIR") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".emu")
SOCK = os.path.join(RUNDIR, "emu.sock")
RAM_INC = os.path.join(os.path.dirname(__file__), "..", "..", "constants", "ram.inc")

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

def write_mem(addr, bytevals):
    """Break (if running), poke consecutive bytes from addr, resume.
    SameBoy has no poke command; `print [$addr] = $val` writes one byte."""
    was = call("STATE").strip() == "running"
    if was: call("BREAK")
    a = int(addr, 16) if isinstance(addr, str) else addr
    for i, v in enumerate(bytevals):
        call(f"RAW print [${a+i:04x}] = ${v & 0xff:02x}")
    if was: call("CONT")
    return [v & 0xff for v in bytevals]

def load_syms():
    """Map RAM/HRAM var names (lowercased) -> address, parsed from constants/ram.inc.
    Only memory addresses (>= $8000) so bit/enum constants (e.g. BTN_*) are ignored."""
    syms = {}
    try:
        with open(RAM_INC) as f:
            for line in f:
                m = re.match(r"\s*DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)", line)
                if m and int(m.group(2), 16) >= 0x8000:
                    syms[m.group(1).lower()] = int(m.group(2), 16)
    except OSError:
        pass
    return syms

def resolve(token):
    """A var name from ram.inc, or a bare hex address -> int address."""
    return load_syms().get(token.lower()) or int(token, 16)

# Named deterministic setters: name -> {mode/value -> [(addr, byte), ...]}.
# Add entries here as more useful live state is identified.
def _keyrepeat(mode):
    d = 0 if mode == "off" else 7            # wKeyDelay; 0 disables (timer underflows)
    return [(resolve("wKeyDelay"), d), (resolve("wKeyDelayTimer"), d)]
PARAMS = {"keyrepeat": _keyrepeat}

if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "read":
        addr, length = resolve(sys.argv[2]), int(sys.argv[3])
        vals = read_mem(addr, length)
        print(" ".join(f"{v:02x}" for v in vals))
    elif len(sys.argv) >= 2 and sys.argv[1] == "write":
        addr, bytevals = resolve(sys.argv[2]), [int(x, 16) for x in sys.argv[3:]]
        write_mem(addr, bytevals)
        print(" ".join(f"{v:02x}" for v in bytevals))
    elif len(sys.argv) >= 2 and sys.argv[1] == "clearwatch":
        # SameBoy `unwatch`/`delete` take an *id*, not an address; clearing by
        # address silently no-ops and leaves stale watchpoints that re-stop the
        # game every resume (looks "stuck"). Loop-remove all, then resume.
        was = call("STATE").strip() == "running"
        call("BREAK")
        call("RAW delete")                       # all breakpoints
        for _ in range(64):
            lst = call("RAW list")
            ids = re.findall(r"^\s*(\d+)\.\s", lst, re.M)   # ids stay stable (no renumber)
            if not ids:
                break
            for i in ids:
                call(f"RAW unwatch {i}")
        print(" ".join(call("RAW list").split()))
        if was:
            call("CONT")
    elif len(sys.argv) >= 3 and sys.argv[1] in PARAMS:
        writes = PARAMS[sys.argv[1]](sys.argv[2].lower())
        for addr, val in writes:
            write_mem(addr, [val])
        print(f"{sys.argv[1]} {sys.argv[2].lower()}: " +
              " ".join(f"${a:04x}=${v:02x}" for a, v in writes))
    else:
        print(call(" ".join(sys.argv[1:])), end="")
