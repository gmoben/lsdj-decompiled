# Build system for the LSDj 9.4.0 disassembly.
#
# Mirrors the pret (https://github.com/pret) convention: hand-maintained RGBDS
# assembly that links into a byte-for-byte reproduction of the original ROM,
# verified with `make compare`.
#
# The original ROM is copyrighted and is NEVER committed. You must supply your
# own legally-obtained copy as `lsdj9_4_0.gb` in the repo root (see README.md).

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
BASEROM  := lsdj9_4_0.gb
ROM_SHA1 := 58ba2e44c726c61d8c91423ec82152319dddc2ca

BUILD  := build
TARGET := $(BUILD)/lsdj9_4_0.gb
OBJ    := $(BUILD)/main.o
SYM    := $(BUILD)/lsdj9_4_0.sym
MAP    := $(BUILD)/lsdj9_4_0.map

.PHONY: all compare verify-rom sym clean tidy baseline
.SECONDARY:

all: $(TARGET)

# --- Assemble + link + fix -------------------------------------------------
$(OBJ): src/main.asm include/hardware.inc $(BUILD)/rgbdscheck.o | $(BASEROM)
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
# rgblink emits a BANK:ADDR Name .sym (BGB / SameBoy / Emulicious format) with
# every named routine. Copy it next to your ROM (as lsdj9_4_0.sym) to debug LSDj
# with named labels -- the fastest way to identify and name more routines.
sym: $(TARGET)
	@printf '==> %s  (%s symbols)\n' "$(SYM)" "$$(wc -l < $(SYM))"
	@echo "    Copy next to your ROM as lsdj9_4_0.sym for named emulator debugging."

# --- Verification ----------------------------------------------------------
# `compare` is the core invariant: the build must equal the original ROM,
# byte for byte. Keep this green through every disassembly change.
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
clean:
	rm -rf $(BUILD)

tidy:
	rm -f $(OBJ) $(BUILD)/rgbdscheck.o $(SYM) $(MAP)

# Helpful message if the user has not supplied the ROM yet.
$(BASEROM):
	@echo "error: base ROM not found: $(BASEROM)"; \
	echo "LSDj is commercial software and is not distributed with this repo."; \
	echo "Place your legally-obtained LSDj 9.4.0 ROM here as '$(BASEROM)'."; \
	echo "Expected sha1: $(ROM_SHA1)"; \
	echo "See README.md for details."; \
	exit 1
