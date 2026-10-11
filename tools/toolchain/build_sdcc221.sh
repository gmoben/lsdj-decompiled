#!/usr/bin/env bash
# Build SDCC 2.2.1 (+ sdcpp, as-gbz80, link-gbz80) natively from the GBDK
# 2.95-2 sources -- the same compiler version as GBDK 2.95-3's sdcc.exe.
#
# Why: the Win32 sdcc.exe is linked with an old Boehm GC which, under Wine,
# frees live objects in larger translation units: garbage `.globl` names, and
# from ~30 functions on, NULL-pointer crashes (exit 5) that depend on the
# heap layout. Built with _NO_GC (the source's own switch), SDCC uses plain
# allocation -- zeroed, as the GC's was -- and runs natively, ~100x faster
# than through Wine. Validated: for every unit tried the native tools emit
# byte-identical asm, objects, listings and linked images to the Win32 ones
# (docs/toolchain.md); tools/match prefers them when present.
#
# One deliberate difference from stock: LSDj's compiler made no SSA copy
# when a function call's result is assigned to a global (the patch below,
# "LSDj's compiler"); the Win32 sdcc.exe does, so it is not byte-identical
# for the three functions where that shows (DedupPhrases, DedupChains,
# DrawFreedTables).
#
# Needs: git, gcc with 32-bit support (-m32; SDCC 2.2.1 assumes 32-bit
# pointers), bison, flex, make. Output: toolchain/sdcc-2.2.1-native/bin/.
#
# `--debug` builds an instrumented copy into toolchain/sdcc-2.2.1-debug/bin/
# for tools/match/trace_alloc.py: the register allocator's own trace
# (z80/ralloc.c's D_ALLOC) switched on, plus prints gated by environment
# variables -- LOOPDBG=1: `FUNC name` at each function, `HID sym in
# preheader -> 0|1` (hasIncomingDefs: whether a last use inside a loop
# extends the live range to the loop end), `DF iter change=N` per dataflow
# pass, `LI`/`HOISTED` for loop invariants; DFV=1: `DFCH block` for each (DFV=2: with its old/new expression sets)
# block whose out-expressions changed. The emitted code is unchanged.
#
# Source: https://juju.nz/src/juju/gbdk-releases (GBDK release tarballs as
# git), tag gbdk-2.95-2, commit 7ee938a8ae65290ddaf9ca234f2ef9777773c6b9.

set -euo pipefail

DEBUG=0
[ "${1:-}" = "--debug" ] && DEBUG=1

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/toolchain/sdcc-2.2.1-native"
[ "$DEBUG" = 1 ] && DEST="$ROOT/toolchain/sdcc-2.2.1-debug"
REPO="https://juju.nz/src/juju/gbdk-releases"
TAG="gbdk-2.95-2"
COMMIT="7ee938a8ae65290ddaf9ca234f2ef9777773c6b9"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "==> fetching $REPO ($TAG)"
git clone -q --depth 1 --branch "$TAG" "$REPO" "$work/gbdk"
got="$(git -C "$work/gbdk" rev-parse HEAD)"
[ "$got" = "$COMMIT" ] || { echo "error: $TAG is $got, expected $COMMIT" >&2; exit 1; }
src="$work/gbdk/sdcc"
cd "$src"

echo "==> patching for a modern host (no codegen changes)"
python3 - <<'EOF'
from pathlib import Path

def edit(path, old, new, count=1):
    p = Path(path); s = p.read_text(errors="surrogateescape")
    assert s.count(old) >= 1, (path, old)
    p.write_text(s.replace(old, new, count), errors="surrogateescape")

# No Boehm GC. Its allocators return zeroed memory and SDCC relies on that.
for h in ("src/SDCCglobl.h", "src/SDCCset.h", "src/SDCChasht.h"):
    s = Path(h).read_text()
    s = s.replace("#define GC_malloc malloc", "#define GC_malloc(n) calloc(1, (n))")
    s = s.replace("#define GC_malloc_atomic malloc", "#define GC_malloc_atomic(n) calloc(1, (n))")
    Path(h).write_text(s)
edit("src/Makefile.in", "LIBS\t\t= -lgc -lm @LIBS@", "LIBS\t\t= -lm")
edit("src/Makefile.in", "all: ports checkconf $(LIBGC) $(TARGET)", "all: ports checkconf $(TARGET)")
# A bit vector that grows (`bitVectSetBit` past its size) must come back
# zero-filled: SDCC reads the new bits as "unset", and the GC's realloc
# (grow = GC_malloc + copy) gave it that; plain realloc leaves the old heap
# contents there (a whole-unit compile showed a dead temporary's key
# "register-assigned" into the following functions).
edit("src/SDCCbitv.c", """    bvp->size = size;
    bvp->bSize= bSize;
    bvp->vect = GC_realloc(bvp->vect, bSize);
    return bvp;""", """    bvp->vect = GC_realloc(bvp->vect, bSize);
    memset(bvp->vect + bvp->bSize, 0, bSize - bvp->bSize);
    bvp->size = size;
    bvp->bSize= bSize;
    return bvp;""")
# Modern flex defines yylineno itself and no longer has YY_PROTO.
edit("src/SDCC.lex", "int   yylineno = 1               ;", "/* yylineno: defined by flex */")
edit("src/SDCC.lex", "int yywrap YY_PROTO((void))", "int yywrap(void)")
# The undocumented `/L1`..`/L4` switches (one label optimisation each) are
# unreachable in the release: the parser only accepts two-character `/x`
# options. Accept the three-character forms too; nothing else changes.
edit("src/SDCCmain.c", "if ( *argv[i] == '/' && strlen(argv[i]) < 3) {",
     "if ( *argv[i] == '/' && strlen(argv[i]) < 4) {")
# sdcpp: an unused GCC target declaration, and headers the 2000-era code
# got implicitly.
edit("support/cpp/i386/i386.h", "extern enum reg_class regclass_map[];",
     "/* extern enum reg_class regclass_map[]; */")
p = Path("support/cpp/cpplib.c"); s = p.read_text(errors="surrogateescape")
i = s.index("#include")
p.write_text(s[:i] + "#include <time.h>\n#include <stdlib.h>\n#include <string.h>\n" + s[i:],
             errors="surrogateescape")
EOF

echo "==> patching to LSDj's compiler (one iCode-generation difference from stock 2.2.1)"
python3 - <<'EOF'
from pathlib import Path

def edit(path, old, new):
    p = Path(path); s = p.read_text(errors="surrogateescape")
    assert s.count(old) == 1, (path, old)
    p.write_text(s.replace(old, new), errors="surrogateescape")

# Stock 2.2.1 copies every value assigned to a non-volatile global through
# a fresh temporary first ("this is SSA"); the register allocator's
# packRegsForAssign merges the copy away again unless the temporary's live
# range was stretched to a loop end, in which case the copy stays and the
# allocation around it changes. LSDj's compiler makes no such copy when the
# value is a function call's result: every other matched function compiles
# identically either way, and DedupPhrases / DedupChains / DrawFreedTables
# ($1F:$5BDE / $5C78 / $5D18) only match without the copy (docs/toolchain.md).
# Dropping the copy for every assignment is too much: ten functions that
# read the global back (LoadSyncSetting ...) then keep the value in a register.
edit("src/SDCCicode.c", """operand *geniCodeAssign (operand *left, operand *right, int nosupdate)
{""", """/* LSDj's compiler (tools/toolchain/build_sdcc221.sh): is `right` the
   result of the function call just generated? */
static int isFreshCallResult (operand *right)
{
    iCode *lic = iCodeChain ? iCodeChain->item : NULL;
    return IS_ITEMP(right) && lic && lic->op == CALL &&
	IC_RESULT(lic) && IC_RESULT(lic)->key == right->key;
}

operand *geniCodeAssign (operand *left, operand *right, int nosupdate)
{""")
edit("src/SDCCicode.c", """    if (IS_TRUE_SYMOP(left) && 
	!isOperandVolatile(left,FALSE) &&
	isOperandGlobal(left)) {
	symbol *sym = NULL;""", """    if (IS_TRUE_SYMOP(left) && 
	!isOperandVolatile(left,FALSE) &&
	isOperandGlobal(left) &&
	!isFreshCallResult(right)) {      /* LSDj's compiler: no SSA copy of a call's result */
	symbol *sym = NULL;""")

# Per-function allocator directives (LSDJ_ALLOC="Func:KIND=spec ..."): six
# functions of the ROM allocate as if a register were held by an occupant no
# symbol accounts for, or a symbol were unspillable at one point
# (docs/toolchain.md, "The phantom register"); a `lsdj-alloc(Func): ...`
# comment in the C source names the window and tools/match/match_function.py
# passes it here. KINDs: BUSYC=lo-hi / BUSYB=lo-hi hold register c / b for
# iCode sequence numbers lo..hi; SPILLSYM=sym spills sym at its definition;
# SPILLAT=sym:seq evicts sym when the walk reaches seq; NOSPILLAT=sym:seq
# keeps sym out of the spill candidates at seq.
edit("src/z80/ralloc.c", """static regs *allocReg (short type)
{""", """/* LSDj's compiler reproduced per function (tools/toolchain/build_sdcc221.sh):
   LSDJ_ALLOC lists directives "Function:KIND=spec" separated by spaces or
   commas; lsdjAlloc(kind, idx, spec) copies the idx-th spec of KIND for the
   function being compiled and returns 1, or returns 0. */
static int lsdjAlloc(const char *kind, int idx, char *spec, int specsz)
{
    const char *env = getenv("LSDJ_ALLOC");
    char buf[2048]; char *tok, *save; int n = 0;
    if (!env || !currFunc) return 0;
    strncpy(buf, env, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (tok = strtok_r(buf, " ,", &save); tok; tok = strtok_r(NULL, " ,", &save)) {
	char *colon = strchr(tok, ':'); char *eq;
	if (!colon) continue;
	eq = strchr(colon + 1, '=');
	if (!eq) continue;
	if ((int)(colon - tok) != (int)strlen(currFunc->name) || strncmp(tok, currFunc->name, colon - tok)) continue;
	if ((int)(eq - colon - 1) != (int)strlen(kind) || strncmp(colon + 1, kind, eq - colon - 1)) continue;
	if (n++ == idx) { strncpy(spec, eq + 1, specsz - 1); spec[specsz - 1] = 0; return 1; }
    }
    return 0;
}

static regs *allocReg (short type)
{""")
edit("src/z80/ralloc.c", """    spillable = bitVectIntersect(spillable,regAssigned);
    return spillable;""", """    spillable = bitVectIntersect(spillable,regAssigned);
    {	/* LSDj's compiler: NOSPILLAT=sym:seq */
	char spec[128]; int n;
	for (n = 0; lsdjAlloc("NOSPILLAT", n, spec, sizeof spec); n++) {
	    char nm[64]; int at = -1; symbol *s; int k;
	    if (sscanf(spec, "%63[^:]:%d", nm, &at) == 2 && at == ic->seq)
		for (s = hTabFirstItem(liveRanges, &k); s; s = hTabNextItem(liveRanges, &k))
		    if (!strcmp(s->name, nm))
			bitVectUnSetBit(spillable, s->key);
	}
    }
    return spillable;""")
edit("src/z80/ralloc.c", """	    /* take away registers from live
	       ranges that end at this instruction */	   
	    deassignLRs (ic, ebbs[i]) ;		
""", """	    /* take away registers from live
	       ranges that end at this instruction */	   
	    deassignLRs (ic, ebbs[i]) ;		
	    {	/* LSDj's compiler: per-function allocator directives (lsdjAlloc) */
		char spec[128]; int d, n;
		static int lsdjHeld[2]; static symbol *lsdjFunc;
		if (lsdjFunc != currFunc) { lsdjHeld[0] = lsdjHeld[1] = 0; lsdjFunc = currFunc; }
		for (d = 0; d < 2; d++) {
		    int lo, hi, in = 0;
		    for (n = 0; lsdjAlloc(d ? "BUSYB" : "BUSYC", n, spec, sizeof spec); n++)
			if (sscanf(spec, "%d-%d", &lo, &hi) == 2 && ic->seq >= lo && ic->seq <= hi)
			    in = 1;
		    if (in && !lsdjHeld[d] && regsZ80[d].isFree) { regsZ80[d].isFree = 0; lsdjHeld[d] = 1; }
		    else if (!in && lsdjHeld[d]) { regsZ80[d].isFree = 1; lsdjHeld[d] = 0; }
		}
		for (n = 0; lsdjAlloc("SPILLAT", n, spec, sizeof spec); n++) {
		    char nm[64]; int at = -1; symbol *s; int k;
		    if (sscanf(spec, "%63[^:]:%d", nm, &at) == 2 && at == ic->seq)
			for (s = hTabFirstItem(liveRanges, &k); s; s = hTabNextItem(liveRanges, &k))
			    if (!strcmp(s->name, nm) && !s->isspilt && bitVectBitValue(regAssigned, s->key))
				spillThis(s);
		}
		if (IC_RESULT(ic) && IS_SYMOP(IC_RESULT(ic)))
		    for (n = 0; lsdjAlloc("SPILLSYM", n, spec, sizeof spec); n++) {
			symbol *rs = OP_SYMBOL(IC_RESULT(ic));
			if (!strcmp(spec, rs->name) && !rs->isspilt && rs->nRegs && !bitVectBitValue(regAssigned, rs->key))
			    spillThis(rs);
		    }
	    }
""")
EOF

if [ "$DEBUG" = 1 ]; then
echo "==> instrumenting (allocator trace, loop/dataflow prints; codegen unchanged)"
python3 - <<'EOF2'
from pathlib import Path

def edit(path, old, new):
    p = Path(path); s = p.read_text(errors="surrogateescape")
    assert s.count(old) == 1, (path, old)
    p.write_text(s.replace(old, new), errors="surrogateescape")

edit("src/z80/ralloc.c", "    D_ALLOC\t\t= 0,", "    D_ALLOC\t\t= 1,")
# names instead of pointers in the allocator trace (symbols, registers, iCode sequence numbers)
for old, new in [
    ('("allocReg: alloced %p\\n", &regsZ80[i])', '("allocReg: alloced %s\\n", regsZ80[i].name)'),
    ('("freeReg: freed %p\\n", reg)', '("freeReg: freed %s\\n", reg->name)'),
    ('("createStackSpil: for sym %p\\n", sym)', '("createStackSpil: for sym %s\\n", sym->name)'),
    ('("spillThis: spilling %p\\n", sym)', '("spillThis: spilling %s\\n", sym->name)'),
    ('("selectSpil: finding spill for ic %p\\n", ic)', '("selectSpil: finding spill for ic seq%d\\n", ic->seq)'),
    ('("getRegGpr: on ic %p\\n", ic)', '("getRegGpr: on ic seq%d\\n", ic->seq)'),
    ('("deassignLRs: in loop on sym %p nregs %u\\n", sym, sym->nRegs)', '("deassignLRs: in loop on sym %s nregs %u\\n", sym->name, sym->nRegs)'),
    ('("reassingLR: on sym %p\\n", sym)', '("reassingLR: on sym %s\\n", sym->name)'),
    ('("positionRegs: on result %p opsum %p line %u\\n", result, opsym, lineno)', '("positionRegs: on result %s opsum %s line %u\\n", result->name, opsym->name, lineno)'),
    ('("tryAllocRegPair: succeded for sym %p\\n", sym)', '("tryAllocRegPair: succeded for sym %s\\n", sym->name)'),
    ('("tryAllocRegPair: failed on sym %p\\n", sym)', '("tryAllocRegPair: failed on sym %s\\n", sym->name)'),
    ('("serialRegAssign: in loop on result %p\\n", sym)', '("serialRegAssign: in loop on result %s (seq%d used%d nregs%d)\\n", sym->name, ic->seq, sym->used, sym->nRegs)'),
    ('    D(D_ALLOC, ("spilSomething: spilling on ic %p\\n", ic));\n\n    /* get something we can spil */\n    ssym = selectSpil(ic,ebp,forSym);\n',
     '    D(D_ALLOC, ("spilSomething: spilling on ic seq%d\\n", ic->seq));\n\n    /* get something we can spil */\n    ssym = selectSpil(ic,ebp,forSym);\n    D(D_ALLOC, ("spilSomething: victim %s\\n", ssym->name));\n'),
    ('("spilSomething: victim %s\\n", ssym->name)', '("spilSomething: victim %s (used%d)\\n", ssym->name, ssym->used)'),
]:
    edit("src/z80/ralloc.c", old, new)
# BUSYC=lo-hi (an experiment knob): register c is held by a phantom occupant for iCode seq in [lo, hi]
edit("src/z80/ralloc.c", """	    /* take away registers from live
	       ranges that end at this instruction */	   
	    deassignLRs (ic, ebbs[i]) ;		
""", """	    /* take away registers from live
	       ranges that end at this instruction */	   
	    deassignLRs (ic, ebbs[i]) ;		
	    if (getenv("BUSYC")) {
		int lo = 0, hi = 0; static int held = 0;
		sscanf(getenv("BUSYC"), "%d-%d", &lo, &hi);
		if (ic->seq >= lo && ic->seq <= hi && !held && regsZ80[0].isFree) { regsZ80[0].isFree = 0; held = 1; }
		else if ((ic->seq < lo || ic->seq > hi) && held) { regsZ80[0].isFree = 1; held = 0; }
	    }
	    if (getenv("BUSYB")) {
		int lo = 0, hi = 0; static int heldb = 0;
		sscanf(getenv("BUSYB"), "%d-%d", &lo, &hi);
		if (ic->seq >= lo && ic->seq <= hi && !heldb && regsZ80[1].isFree) { regsZ80[1].isFree = 0; heldb = 1; }
		else if ((ic->seq < lo || ic->seq > hi) && heldb) { regsZ80[1].isFree = 1; heldb = 0; }
	    }
	    /* SPILLAT=name:seq,...: these symbols are evicted (spilt) when the walk reaches seq,
	       as a phantom allocation at that point would do */
	    if (getenv("SPILLAT")) {
		char buf[256]; char *tok; strncpy(buf, getenv("SPILLAT"), 255); buf[255] = 0;
		for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
		    char nm[64]; int at = -1; symbol *s; int k;
		    if (sscanf(tok, "%63[^:]:%d", nm, &at) == 2 && at == ic->seq)
			for (s = hTabFirstItem(liveRanges, &k); s; s = hTabNextItem(liveRanges, &k))
			    if (!strcmp(s->name, nm) && !s->isspilt && bitVectBitValue(regAssigned, s->key)) {
				D(D_ALLOC, ("SPILLAT: evicting %s at seq%d\\n", s->name, ic->seq));
				spillThis(s);
			    }
		}
	    }
	    /* SPILLSYM=name,name: these symbols are spilt at their definition (as a use before the
	       definition or a volatile would do, without changing the code elsewhere) */
	    if (getenv("SPILLSYM") && IC_RESULT(ic) && IS_SYMOP(IC_RESULT(ic))) {
		symbol *rs = OP_SYMBOL(IC_RESULT(ic));
		char buf[256]; char *tok; strncpy(buf, getenv("SPILLSYM"), 255); buf[255] = 0;
		for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
		    if (!strcmp(tok, rs->name) && !rs->isspilt && rs->nRegs && !bitVectBitValue(regAssigned, rs->key)) {
			D(D_ALLOC, ("SPILLSYM: spilling %s at seq%d\\n", rs->name, ic->seq));
			spillThis(rs);
		    }
	    }
""")
edit("src/z80/ralloc.c", """    for (i=0;i< _nRegs;i++ )
	regsZ80[i].isFree = 1;
}""", """    for (i=0;i< _nRegs;i++ )
	regsZ80[i].isFree = 1;
    { int h; for (h = 0; h < 8; h++) heldRegs[h] = 0; }
    if (regAssigned) {
	int k; symbol *s;
	for (k = 0; k < regAssigned->size; k++)
	    if (bitVectBitValue(regAssigned, k)) {
		const char *nm = "?";
		for (s = hTabFirstItem(liveRanges, &i); s; s = hTabNextItem(liveRanges, &i))
		    if (s->key == k) { nm = s->name; break; }
		D(D_ALLOC, ("LEAKBIT: key %d (%s) still regAssigned at %s end; regs %s %s\\n", k, nm,
			    currFunc ? currFunc->name : "?",
			    s && s->regs[0] ? s->regs[0]->name : "-", s && s->regs[1] ? s->regs[1]->name : "-"));
	    }
    }
}""")
edit("src/z80/ralloc.c", 'D(D_ALLOC, ("\\n-> z80_assignRegisters: entered.\\n"));',
     'D(D_ALLOC, ("\\n-> z80_assignRegisters: entered %s (regAssigned bits carried over: %d).\\n", currFunc ? currFunc->name : "?", regAssigned ? bitVectnBitsOn(regAssigned) : 0));')
edit("src/SDCCopt.c", """    /* if nothing passed then return nothing */
    if (!ic)
	return NULL ;
""", """    /* if nothing passed then return nothing */
    if (!ic)
	return NULL ;
    if (getenv("LOOPDBG"))
	printf("FUNC %s\\n", currFunc ? currFunc->name : "?");
""")
# Hypothesis knobs (experiments on what a modified allocator might have done):
#   DEASSIGN_STRICT=1  a dying symbol's registers are freed one iCode later (liveTo < seq)
#   CND_ALLOC=1        conditional temps (compare feeding the next IFX) get a register too
#   HANDOVER_NONE=1    no hand-over of a dying operand's registers to the result
#   ALLOC_BFIRST=1     single registers are allocated b first
#   NO_POSITION=1      positionRegs disabled
#   IFX_LATE=1         symbols dying at an IFX are freed at the following iCode
#   PAIR_SPILL_FIRST_LOADS=1  the same, only for a pair defined by an assignment from memory or
#                      a pointer read (SynthWriteWaveCol's `:= wSynthRenderDst`, BandPass's BufB word)
#   PAIR_SPILL_FIRST=1 a two-register request with no free pair spills (the stock victims) until
#                      a pair is free and then takes the pair in order (c b), instead of taking the
#                      free register first and the spilt one second (b c)
#   HOLD_CALLRES=1     a call result's register is not freed when the result dies but held until
#                      the next two-register request (released just before it)
#   HOLD_PAIR_HI=1     a dying register pair's second register (regs[1]) is held the same way
#   HANDOVER_SPILT=1   the hand-over of a dying operand's registers to the result happens even
#                      when the result is spilt or rematerialisable (stock skips those)
#   PAIR_NORMALIZE=1   (codegen) a two-register temporary allocated b then c, when defined by an
#                      assignment from memory or a pointer read, has its registers swapped to c b
#                      at that definition, so it is loaded as the pair bc and pushed as one
#   KEYOF=name:K,...   force the operand key of global symbol `name` to K (a global's key is
#                      assigned on its first use in the unit and kept for later functions,
#                      where it can coincide with a temp's key: isOperandEqual compares keys)
edit("src/SDCCicode.c", """    if (!sym->key)
	sym->key = ++operandKey ;

    /* if this an implicit variable, means struct/union */""", """    if (!sym->key) {
	sym->key = ++operandKey ;
	if (getenv("KEYDBG") && !sym->level)
	    printf("KEY %s %d in %s\\n", sym->name, sym->key, currFunc ? currFunc->name : "?");
    }
    if (getenv("KEYOF") && !sym->level) {
	char buf[512]; char *tok; strncpy(buf, getenv("KEYOF"), 511); buf[511] = 0;
	for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
	    char *colon = strchr(tok, ':');
	    if (colon && !strncmp(tok, sym->name, colon - tok) && !sym->name[colon - tok])
		sym->key = atoi(colon + 1);
	}
    }

    /* if this an implicit variable, means struct/union */""")
edit("src/z80/ralloc.c", """	symbol *psym= NULL;
	/* if it does not end here */
	if (sym->liveTo > ic->seq )
	    continue ;
""", """	symbol *psym= NULL;
	/* if it does not end here */
	if (sym->liveTo > ic->seq )
	    continue ;
	if (getenv("DEASSIGN_STRICT") && sym->liveTo == ic->seq)
	    continue ;
	if (getenv("IFX_LATE") && ic->op == IFX && sym->liveTo == ic->seq)
	    continue ;
""")
edit("src/z80/ralloc.c", """	    /* if the type is marked as a conditional */
	    if (sym->regType == REG_CND)
		continue ;
""", """	    /* if the type is marked as a conditional */
	    if (sym->regType == REG_CND && !getenv("CND_ALLOC"))
		continue ;
""")
edit("src/z80/ralloc.c", """		(result = OP_SYMBOL(IC_RESULT(ic))) && /* has a result */
		result->liveTo > ic->seq &&            /* and will live beyond this */""",
"""		!getenv("HANDOVER_NONE") &&
		(result = OP_SYMBOL(IC_RESULT(ic))) && /* has a result */
		result->liveTo > ic->seq &&            /* and will live beyond this */""")
edit("src/z80/ralloc.c", """    for ( i = 0 ; i < _nRegs ; i++ ) {
	/* For now we allocate from any free */
	if (regsZ80[i].isFree ) {""", """    for ( i = 0 ; i < _nRegs ; i++ ) {
	int k = getenv("ALLOC_BFIRST") ? _nRegs - 1 - i : i;
	/* For now we allocate from any free */
	if (regsZ80[k].isFree ) {
	    i = k;""")
edit("src/z80/ralloc.c", """    /* if the result has been spilt then cannot share */
    if (opsym->isspilt)
	return ;""", """    /* if the result has been spilt then cannot share */
    if (opsym->isspilt || getenv("NO_POSITION"))
	return ;""")
edit("src/z80/gen.c", """    /* must be in a register */
    sym->aop = op->aop = aop = newAsmop(AOP_REG);
    aop->size = sym->nRegs;
    for ( i = 0 ; i < sym->nRegs ;i++)
        aop->aopu.aop_reg[i] = sym->regs[i];
}""", """    /* must be in a register */
    if (getenv("PAIR_NORMALIZE") && result && sym->nRegs == 2 && sym->regs[0] && sym->regs[1] &&
	sym->regs[0]->rIdx == B_IDX && sym->regs[1]->rIdx == C_IDX &&
	((ic->op == '=' && !POINTER_SET(ic) && !IS_OP_LITERAL(IC_RIGHT(ic))) || ic->op == GET_VALUE_AT_ADDRESS)) {
	regs *t = sym->regs[0]; sym->regs[0] = sym->regs[1]; sym->regs[1] = t;
	emitcode("", "; PAIR_NORMALIZE: %s now c b", sym->name);
    }
    sym->aop = op->aop = aop = newAsmop(AOP_REG);
    aop->size = sym->nRegs;
    for ( i = 0 ; i < sym->nRegs ;i++)
        aop->aopu.aop_reg[i] = sym->regs[i];
}""")
edit("src/z80/ralloc.c", """static regs *allocReg (short type)
{""", """static int heldRegs[8];
static void releaseHeld(void)
{
    int i;
    for (i = 0; i < 8; i++)
	if (heldRegs[i]) { D(D_ALLOC, ("releaseHeld: %s\\n", regsZ80[heldRegs[i] - 1].name)); regsZ80[heldRegs[i] - 1].isFree = 1; heldRegs[i] = 0; }
}
static void holdReg(regs *reg)
{
    int i;
    for (i = 0; i < 8; i++)
	if (!heldRegs[i]) { heldRegs[i] = (reg - regsZ80) + 1; D(D_ALLOC, ("holdReg: %s\\n", reg->name)); return; }
}
static regs *allocReg (short type)
{""")
edit("src/z80/ralloc.c", """bool tryAllocatingRegPair(symbol *sym)
{
    int i;
    wassert(sym->nRegs == 2);""", """bool tryAllocatingRegPair(symbol *sym)
{
    int i;
    wassert(sym->nRegs == 2);
    releaseHeld();""")
edit("src/z80/ralloc.c", """	    /* free the remaining */
	    for (; i < sym->nRegs ; i++) {
		if (psym) {
		    if (!symHasReg(psym,sym->regs[i]))
			freeReg(sym->regs[i]);
		} else
		    freeReg(sym->regs[i]);""", """	    /* free the remaining */
	    for (; i < sym->nRegs ; i++) {
		if ((getenv("HOLD_CALLRES") && sym->isCallRes) ||
		    (getenv("HOLD_PAIR_HI") && sym->nRegs == 2 && i == 1)) {
		    holdReg(sym->regs[i]);
		    continue;
		}
		if (psym) {
		    if (!symHasReg(psym,sym->regs[i]))
			freeReg(sym->regs[i]);
		} else
		    freeReg(sym->regs[i]);""")
edit("src/z80/ralloc.c", """		! result->isspilt        &&            /* and does not already have them */
		! result->remat          &&""", """		(getenv("HANDOVER_SPILT") || (! result->isspilt && ! result->remat)) &&""")
edit("src/z80/ralloc.c", """		/* else we assign registers to it */		
		regAssigned = bitVectSetBit(regAssigned,sym->key);""", """		/* else we assign registers to it */		
		regAssigned = bitVectSetBit(regAssigned,sym->key);
		if (ic->op == CALL || ic->op == PCALL) sym->isCallRes = 1;""")
edit("src/SDCCsymt.h", """    unsigned isspilt    :1      ;  /* has to be spilt */""", """    unsigned isspilt    :1      ;  /* has to be spilt */
    unsigned isCallRes  :1      ;  /* LSDj experiment knobs: the symbol is a call's result */""")
edit("src/z80/ralloc.c", """		if ((sym->nRegs == 2)&&tryAllocatingRegPair(sym)) {
		}
		else {""", """		if (sym->nRegs == 2 && (getenv("PAIR_SPILL_FIRST") ||
		    (getenv("PAIR_SPILL_FIRST_LOADS") &&
		     ((ic->op == '=' && !POINTER_SET(ic) && !IS_OP_LITERAL(IC_RIGHT(ic))) || ic->op == GET_VALUE_AT_ADDRESS)))) {
		    int guard = 0;
		    while (nFreeRegs(0) < 2 && guard++ < 4 &&
			   !bitVectIsZero(computeSpillable(ic)) && spilSomething(ic, ebbs[i], sym))
			;
		    D(D_ALLOC, ("PAIR_SPILL_FIRST: %d free after spilling\\n", nFreeRegs(0)));
		}
		if ((sym->nRegs == 2)&&tryAllocatingRegPair(sym)) {
		}
		else {""")
#   NOEXTEND=name,...  temp `name` is not extended to its loop's end by hasIncomingDefs
#   NOSSA_OPS=chars    no SSA copy either when the value is the result of the iCode just
#                      generated and its op is one of the chars (e.g. NOSSA_OPS='+<^&|-*'; '@' for a
#                      pointer read, '=' for an assignment, 'c' for a cast)
edit("src/SDCCicode.c", """    return IS_ITEMP(right) && lic && lic->op == CALL &&
	IC_RESULT(lic) && IC_RESULT(lic)->key == right->key;
}""", """    if (getenv("NOSSA_OPS") && IS_ITEMP(right) && lic && IC_RESULT(lic) && IC_RESULT(lic)->key == right->key) {
	int op = lic->op == GET_VALUE_AT_ADDRESS ? '@' : lic->op == CAST ? 'c' : lic->op == LEFT_OP ? '<' :
	    lic->op == RIGHT_OP ? '>' : lic->op == BITWISEAND ? '&' : lic->op;
	if (op < 256 && strchr(getenv("NOSSA_OPS"), op))
	    return 1;
    }
    return IS_ITEMP(right) && lic && lic->op == CALL &&
	IC_RESULT(lic) && IC_RESULT(lic)->key == right->key;
}""")
edit("src/SDCClrange.c", """	if (ebp->partOfLoop && 
	    hasIncomingDefs(ebp->partOfLoop,op)) {
	    torange = findLoopEndSeq(ebp->partOfLoop);
	}
""", """	if (ebp->partOfLoop) {
	    int hid = hasIncomingDefs(ebp->partOfLoop,op);
	    if (hid && getenv("NOEXTEND") && strstr(getenv("NOEXTEND"), OP_SYMBOL(op)->name))
		hid = 0;
	    if (getenv("LOOPDBG")) {
		eBBlock *ph = ebp->partOfLoop->entry->preHeader;
		printf("HID %s in %s -> %d (defs", OP_SYMBOL(op)->name,
		       (ph && ph->entryLabel) ? ph->entryLabel->name : "?", hid);
		if (OP_DEFS(op)) { int b; for (b = 0; b < OP_DEFS(op)->size; b++) if (bitVectBitValue(OP_DEFS(op), b)) printf(" ic%d", b); }
		printf("; preheader outDefs");
		if (ph && ph->outDefs) { int b; for (b = 0; b < ph->outDefs->size; b++) if (bitVectBitValue(ph->outDefs, b)) printf(" ic%d", b); }
		printf(")\\n");
	    }
	    if (hid)
		torange = findLoopEndSeq(ebp->partOfLoop);
	}
""")
edit("src/SDCCdflow.c", """	    change += ! isSetsEqualWith(ebbs[i]->outExprs,oldOut,isCseDefEqual);
	}

	if (!change) /* iterate till no change */
""", """	    {
		int ch = ! isSetsEqualWith(ebbs[i]->outExprs,oldOut,isCseDefEqual);
		if (ch && getenv("DFV")) {
		    printf("DFCH %s\\n", ebbs[i]->entryLabel ? ebbs[i]->entryLabel->name : "?");
		    if (getenv("DFV")[0] == '2') {   /* the expressions that came or went */
			set *sp; int which;
			for (which = 0; which < 2; which++)
			    for (sp = which ? ebbs[i]->outExprs : oldOut; sp; sp = sp->next) {
				cseDef *cd = sp->item; iCode *dic = cd->diCode;
				printf("  %s %s = op%d %s %s\\n", which ? "NEW" : "OLD",
				       IS_SYMOP(cd->sym) ? OP_SYMBOL(cd->sym)->name : "?", dic->op,
				       (IC_LEFT(dic) && IS_SYMOP(IC_LEFT(dic))) ? OP_SYMBOL(IC_LEFT(dic))->name : (IC_LEFT(dic) ? "lit" : "-"),
				       (IC_RIGHT(dic) && IS_SYMOP(IC_RIGHT(dic))) ? OP_SYMBOL(IC_RIGHT(dic))->name : (IC_RIGHT(dic) ? "lit" : "-"));
			    }
		    }
		}
		change += ch;
	    }
	}

	if (getenv("LOOPDBG"))
	    printf("DF iter change=%d\\n", change);
	if (!change) /* iterate till no change */
""")
edit("src/SDCCloop.c", """		addSet(&lInvars,ivar);
""", """		addSet(&lInvars,ivar);
		if (getenv("LOOPDBG"))
		    printf("LI %s ic%d %s\\n",
			   theLoop->entry->entryLabel ? theLoop->entry->entryLabel->name : "?",
			   ic->key, OP_SYMBOL(IC_RESULT(ic))->name);
""")
edit("src/SDCCloop.c", """	    preHdr->defSet = bitVectSetBit (preHdr->defSet,cdp->diCode->key);
""", """	    if (getenv("LOOPDBG"))
		printf("HOISTED ic%d %s -> %s\\n", cdp->diCode->key,
		       OP_SYMBOL(IC_RESULT(cdp->diCode))->name,
		       preHdr->entryLabel ? preHdr->entryLabel->name : "?");
	    preHdr->defSet = bitVectSetBit (preHdr->defSet,cdp->diCode->key);
""")
EOF2
fi

CFLAGS="-O1 -g -w -fcommon -std=gnu89 -m32 -D_NO_GC -DYY_NO_UNISTD_H -D_POSIX_C_SOURCE=200112L -DHAVE_STRERROR"
echo "==> configure"
CC="gcc -m32" CFLAGS="$CFLAGS" ./configure > "$work/configure.log" 2>&1 || { tail -20 "$work/configure.log"; exit 1; }
sed -i "s|^CFLAGS          = .*|CFLAGS          = $CFLAGS|" Makefile.common

echo "==> build (sdcc, sdcpp, as-gbz80, link-gbz80)"
log="$work/build.log"
{
    make -C src CC="gcc -m32"       # the ports build in sub-makes; run twice
    make -C src CC="gcc -m32"
    make -C support/cpp CC="gcc -m32" CFLAGS="$CFLAGS"
    make -C as CC="gcc -m32"
    make -C link CC="gcc -m32"
} > "$log" 2>&1 || { grep -n "error" "$log" | head -20; tail -30 "$log"; exit 1; }

mkdir -p "$DEST/bin"
for t in sdcc sdcpp as-gbz80 link-gbz80; do
    [ -x "bin/$t" ] || { echo "error: bin/$t was not built (see $log)" >&2; grep -n "error" "$log" | head -20; tail -30 "$log"; exit 1; }
    cp "bin/$t" "$DEST/bin/$t"
done
echo "$COMMIT" > "$DEST/SOURCE_COMMIT"
"$DEST/bin/sdcc" --version 2>&1 | head -1
echo "==> installed to $DEST/bin"
