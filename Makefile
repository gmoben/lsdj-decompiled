# Build system for the LSDj 9.4.0 disassembly.
#
# Mirrors the pret (https://github.com/pret) convention: hand-maintained RGBDS
# assembly that links into a byte-for-byte reproduction of the original ROM,
# verified with `make compare`.
#
# The ROM itself is not committed. Download it from littlesounddj.com (free
# for personal use) and drop it in the repo root as `lsdj9_4_0.gb`, or pass
# BASEROM=/path/to/it on the make command line (see README.md).

# --- RGBDS toolchain -------------------------------------------------------
# Override RGBDS to point at a local toolchain, e.g. `make RGBDS=rgbds-1.0.1/`.
RGBDS   ?=
RGBASM  ?= $(RGBDS)rgbasm
RGBLINK ?= $(RGBDS)rgblink
RGBFIX  ?= $(RGBDS)rgbfix
RGBGFX  ?= $(RGBDS)rgbgfx

RGBASMFLAGS  ?= -i include/ -i src/
RGBLINKFLAGS ?=
# Header is already baked into the ROM data; `-v` validates and recomputes the
# header + global checksums (a no-op while the build matches the original).
RGBFIXFLAGS  ?= -v

# --- Files -----------------------------------------------------------------
# BASEROM: your supplied original (input). TARGET: what we build (output).
# Either copy your ROM into the repo root as lsdj9_4_0.gb, or point BASEROM at
# it anywhere on disk:  make BASEROM=/path/to/your/lsdj9_4_0.gb compare
BASEROM  ?= lsdj9_4_0.gb
ROM_SHA1 := 58ba2e44c726c61d8c91423ec82152319dddc2ca
# The generators (carve/main.asm) read the same path via this env var.
export LSDJ_BASEROM := $(BASEROM)

BUILD  := build
TARGET := $(BUILD)/lsdj9_4_0.gb
OBJ    := $(BUILD)/main.o
SYM    := $(BUILD)/lsdj9_4_0.sym
MAP    := $(BUILD)/lsdj9_4_0.map

.PHONY: all compare verify-rom sym carve listing audit match clean tidy baseline
.SECONDARY:

all: $(TARGET)

# --- Carve: generate the disassembly from your ROM + the tracked symbol maps -
# src/bankNN.asm and src/main.asm are GENERATED (they are the ROM re-encoded as
# text, so they are never committed). A fresh clone builds them here from your
# ROM and the hand-curated src/bankNN.sym + carve metadata.
CARVED_ASM := $(wildcard src/bank*.sym)
carve: | $(BASEROM)
	tools/carve_clean.py

# --- Matching C (Track 2) --------------------------------------------------
# Functions under c/bankNN/*.c compile byte-for-byte with the period SDCC
# 2.2.1 -- built natively from the GBDK 2.95-2 sources
# (tools/toolchain/build_sdcc221.sh), identical in output to GBDK 2.95-3's
# Win32 binaries -- plus tools/match/lsdj.peep. `make match` verifies them all, writes their bytes to
# build/c/ and the manifest c/matched.json; the carve then renders those ROM
# ranges as INCBINs of the compiled bytes -- so the C is the source of those
# bytes and a C file that stops matching fails the build. Without a compiler
# the same ranges INCBIN the ROM, so `compare` works everywhere.
C_SRCS := $(wildcard c/bank*/*.c) $(wildcard c/include/*.h)
C_TOOLS := tools/match/verify_all.py tools/match/match_function.py tools/match/peep.py tools/match/lsdj.peep
# (Only the native build: the Win32 sdcc.exe crashes under Wine on units
# the size of c/bank03/InstrScreen.c.)
HAVE_C_TOOLCHAIN := $(shell test -x toolchain/sdcc-2.2.1-native/bin/sdcc && echo 1)
ifeq ($(HAVE_C_TOOLCHAIN),1)
C_STAMP := $(BUILD)/c/.stamp
# Not an intermediate: `.SECONDARY:` above makes every target one, and make
# would then never rebuild a *missing* stamp whose dependents look up to date.
.NOTINTERMEDIATE: $(C_STAMP)
$(C_STAMP): $(C_SRCS) $(C_TOOLS) $(CARVED_ASM) | $(BASEROM)
	@mkdir -p $(@D)
	tools/match/verify_all.py --write $(BUILD)/c
	@touch $@
endif

match: | $(BASEROM)
	tools/match/verify_all.py --write $(BUILD)/c
	@mkdir -p $(BUILD)/c && touch $(BUILD)/c/.stamp

# Auto-carve on a fresh clone: main.asm is the linker entry point; if it is
# missing, generate the whole tree first.
src/main.asm: $(CARVED_ASM) tools/carve_clean.py tools/gen_main.py c/matched.json $(C_STAMP) | $(BASEROM)
	tools/carve_clean.py

# --- Assemble + link + fix -------------------------------------------------
src/ramsections.asm: constants/ram.inc tools/gen_ram_sections.py tools/gen_sym.py
	tools/gen_ram_sections.py

$(OBJ): src/main.asm src/ramsections.asm include/hardware.inc $(BUILD)/rgbdscheck.o | $(BASEROM)
	@mkdir -p $(@D)
	$(RGBASM) $(RGBASMFLAGS) -o $@ $<

$(BUILD)/rgbdscheck.o: rgbdscheck.asm
	@mkdir -p $(@D)
	$(RGBASM) -o $@ $<

$(TARGET): $(OBJ)
	@mkdir -p $(@D)
	$(RGBLINK) $(RGBLINKFLAGS) -n $(SYM) -m $(MAP) -o $@ $<
	$(RGBFIX) $(RGBFIXFLAGS) $@

# --- Symbol file for emulator-assisted debugging ---------------------------
# rgblink's $(SYM) lists every named routine (BGB / SameBoy / Emulicious format)
# but NOT our RAM variables (EQU constants are not exported). `make sym` enriches
# $(SYM) in place with constants/ram.inc (idempotent), so build/lsdj9_4_0.sym is
# the single comprehensive symbol file -- correct next to the built ROM -- and
# then deploys a copy beside each other ROM you open. The copies are required:
# the debugger auto-loads a .sym only from beside the ROM, matching its basename,
# and the root + .emu ROMs aren't in build/. (A later relink resets $(SYM) to
# routines-only; re-run `make sym`.)
ROMSYM := $(BASEROM:.gb=.sym)
sym: $(TARGET)
	tools/gen_sym.py $(SYM) constants/ram.inc -o $(SYM)
	cp $(SYM) $(ROMSYM)
	@# Deploy a SECTIONS-ONLY map: Emulicious prefers <rom>.map over .sym as
	@# its single symbols source, but falls back to the .sym (which carries
	@# our curated comments) iff the map contributes zero symbols. Stripping
	@# the "$$addr = name" lines keeps the named sections AND the rich .sym.
	sed '/ = /d' $(MAP) > $(ROMSYM:.sym=.map)
	@if [ -e .emu/lsdj_song.gb ]; then cp $(SYM) .emu/lsdj_song.sym; \
	  echo "    deployed beside emulator ROM -> .emu/lsdj_song.sym"; fi
	@printf '==> %s (+ map with named RAM/SRAM sections) -> %s\n' "$(SYM)" "$(ROMSYM)"
	@echo "    Auto-loaded by Emulicious/BGB/SameBoy from beside the ROM."

# --- Verification ----------------------------------------------------------
# `compare` is the core invariant: the build must equal the original ROM,
# byte for byte. Keep this green through every disassembly change.
# Full annotated disassembly in one file -- the local stand-in for pasting the
# Emulicious debugger view (regenerate any time; git-ignored).
listing: src/main.asm
	tools/gen_listing.py -o $(BUILD)/listing.asm

# Cross-check a pasted Emulicious export against our symbols: prints only the
# actionable gaps (unsymboled code/data) vs cosmetic operand-quirks.
audit: sym
	tools/audit_export.py emulicious.out

compare: $(TARGET) | $(BASEROM)
	cmp $(BASEROM) $(TARGET)
	@printf '%s  %s\n' "$(ROM_SHA1)" "$(TARGET)" | sha1sum -c -
	@echo "==> compare OK: build matches the original ROM byte-for-byte."

# Confirm a supplied base ROM is the exact expected revision.
verify-rom: | $(BASEROM)
	@printf '%s  %s\n' "$(ROM_SHA1)" "$(BASEROM)" | sha1sum -c -

# Regenerate the bootstrap baseline from the base ROM (one-time; clobbers
# hand edits to src/main.asm -- see tools/gen_baseline.py).
baseline: | $(BASEROM)
	tools/gen_baseline.py $(BASEROM) -o src/main.asm

# --- Housekeeping ----------------------------------------------------------
# `clean` also removes the deployed root symbol copy (.emu/ is gitignored
# runtime) and the generated disassembly, which may INCBIN build/c/ bytes.
clean:
	rm -rf $(BUILD) $(ROMSYM) src/bank*.asm src/main.asm src/ramsections.asm

tidy:
	rm -f $(OBJ) $(BUILD)/rgbdscheck.o $(SYM) $(MAP) $(ROMSYM)

# Helpful message if the user has not supplied the ROM yet.
$(BASEROM):
	@echo "error: base ROM not found: $(BASEROM)"; \
	echo "Download LSDj 9.4.0 from https://www.littlesounddj.com/ (free for"; \
	echo "personal use) and place it here as '$(BASEROM)', or pass"; \
	echo "BASEROM=/path/to/your/rom.gb. Expected sha1: $(ROM_SHA1)"; \
	exit 1
