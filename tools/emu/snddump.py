#!/usr/bin/env python3
"""Annotated snapshot of the sound-engine WRAM block: read `len` bytes from
`start` (live, via emu.py) and print them 4 per row tagged with the per-channel
`[4]` array names. Labels marked `?` / `?GAP` are still unidentified hypotheses.

Usage: tools/emu/snddump.py <start-hex> <len>"""
import subprocess, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EMU = os.path.join(ROOT, "tools", "emu", "emu.py")
KNOWN = {
    0xc100: 'wChanNextPhrase', 0xc108: '?', 0xc10e: 'wTableTransposePtr', 0xc110: 'wChanReadPtr(16)',
    0xc118: '?', 0xc11e: 'wTableCmd1Ptr', 0xc126: 'wTableCmd2Ptr', 0xc128: 'wChanCmdValPtr(16)',
    0xc136: 'wTableCmd1ValPtr', 0xc13e: 'wTableCmd2ValPtr', 0xc140: 'wChanInstrPtr(16)', 0xc148: '?GAP',
    0xc150: 'wChanGroove', 0xc154: 'wChanTablePlayed', 0xc158: 'wChanGrooveRow', 0xc15c: '?GAP',
    0xc160: 'wChanNoteCountdown', 0xc164: 'wChanTableCountdown', 0xc168: 'wChanPhrase', 0xc16c: 'wChanPhraseRow',
    0xc170: 'wChanChain', 0xc174: '?GAP', 0xc178: '?GAP', 0xc17c: 'wChanChainRow',
    0xc200: 'wChanSongRow', 0xc204: 'wChanTable', 0xc208: 'wChanTableRow1', 0xc20c: 'wChanTableRow2',
    0xc210: 'wChanInstr', 0xc214: '?GAP', 0xc218: '?GAP', 0xc21c: '?GAP', 0xc220: '?GAP',
}
start = int(sys.argv[1], 16); length = int(sys.argv[2])
out = subprocess.run(['python3', EMU, 'read', sys.argv[1], sys.argv[2]],
                     capture_output=True, text=True).stdout.split()
vals = [int(x, 16) for x in out]
for i in range(0, len(vals), 4):
    a = start + i; row = ' '.join(f'{v:02x}' for v in vals[i:i+4])
    print(f'${a:04x}: {row}   {KNOWN.get(a, "")}')
