/*
 * lsdj.h -- shared declarations for the matching C (Track 2).
 *
 * Every name here is a real LSDj symbol (src/bankNN.sym, constants/ram.inc,
 * include/hardware.inc, or the SRAM map in tools/gen_sym.py); the harness
 * links each extern at that address. Prototypes are part of the match -- an
 * argument's width or a return type changes the code SDCC emits -- so a
 * signature is settled here once, by the functions that call it. Data that
 * only one module uses (its strings and tables) is declared in that module.
 *
 * How LSDj's source spelled things, as far as the bytes tell:
 *   - `extern volatile sfr UBYTE` for the $FFxx registers and the HRAM
 *     variables it accesses with ldh; a plain extern compiles to the 3-byte
 *     `ld a,(nnnn)` form. hGameboyType is read that way (SDCC 2.2.1 crashes
 *     on `if (sfr)`, so LSDj's source must have declared it plainly).
 *   - The working song in SRAM is addressed with numeric constants, not
 *     linker symbols: SDCC swaps a *literal* left operand of `+` to the
 *     right, an extern's address it does not (InstrSelectNextUsed tells them
 *     apart), so the SramXxx names below are literal-pointer macros.
 *   - Banked calls go through FarCall/FarCallArg (bank 0, hand-written asm).
 *     Most call sites pass the function itself (`ld bc,#fn`); others pass
 *     it cast through an integer, which SDCC loads byte-wise (`ld c,#<fn;
 *     ld b,#>fn`) -- FAR_FN(fn) reproduces those. FarCallArg's address
 *     parameter is an integer (its call sites cast with (UWORD)).
 */

#ifndef LSDJ_H
#define LSDJ_H

#ifdef __clang__  /* editor tooling only: SDCC 2.2.1 keyword (never set by SDCC) */
#define sfr
#endif

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned int UWORD;     /* SDCC: int is 16 bits */
typedef int WORD;

/* --- hardware and HRAM ---------------------------------------------------- */

extern volatile sfr UBYTE VBK_REG;          /* $FF4F rVBK */
extern volatile sfr UBYTE hBgPalette;       /* $FF8C */
extern volatile sfr UBYTE hBgDigitPalette;  /* $FF8D */
extern volatile sfr UBYTE hCurrentBank;     /* $FF8E */
extern UBYTE hGameboyType;                  /* $FF90 */
extern UBYTE rROMB0;                        /* $2000, the MBC5 ROM bank register */
extern UBYTE rRAMG;                         /* $0000, MBC5 SRAM enable ($0A on) */
extern UBYTE rRAMB;                         /* $4000, MBC5 SRAM bank */

/* GBDK's SWITCH_ROM_MBC1 is `*(UBYTE *)0x2000 = n`, which SDCC compiles through
 * de (`ld de,#0x2000; ld a,#n; ld (de),a`); the ROM has the named-variable form
 * (`ld hl,#0x2000; ld (hl),#n`), so LSDj wrote the register as a variable. It
 * also keeps the HRAM shadow the far-call helpers rely on. */
#define SWITCH_ROM(n) hCurrentBank = (n); rROMB0 = (n)

/* --- WRAM ----------------------------------------------------------------- */

extern UBYTE wCurrentScreen;                /* $C401 */
extern UBYTE wButtonsSame;                  /* $C408: 1 if the buttons are unchanged since last frame */
extern UBYTE wSongCol;                      /* $C41D: song cursor column (channel) */
extern UBYTE wSongRow;                      /* $C41E: song cursor row */
extern UBYTE wInstrField;                   /* $C427: instrument screen cursor row */
extern char wSampleNoteString[];            /* $C43A: [7] shared name buffer */
extern UBYTE wSongReadBuf[];                /* $C46D, [16] ReadSongBytes buffer */
extern UBYTE wUsedTableMap[];               /* $C47D, [32] tables referenced by instruments; also a scratch buffer */
extern UBYTE wPlayMode;                     /* $C4D2 */
extern UBYTE wShadowDigitPal;               /* $C566 */
extern UBYTE wShadowPal;                    /* $C567 */
extern UBYTE wSelectDigitPal;               /* $C568 */
extern UBYTE wSelectPal;                    /* $C569 */
extern UBYTE *wVramCursor;                  /* $C573 */
extern UBYTE wInstrument;                   /* $C576, the instrument being edited */
extern UBYTE wStrBuffer[];                  /* $C6A9, 20 chars + NUL */
extern UBYTE wMsgTimer;                     /* $C7A9 */
extern UBYTE *wMsgStrPtr;                   /* $C7AA */
extern UBYTE wMsgPanelShown;                /* $C7AC */
extern UBYTE wInstrEditLatch;               /* $C906: instrument screen edit latch */
extern char *wChainScratchPtr;              /* $C948 */
extern UBYTE wSyncMode;                     /* $CB66 */
extern UBYTE wSyncMode2;                    /* $CB67 */

/* --- the working song in SRAM (docs/data_model.md; tools/gen_sym.py) ------- */

#define SramPhraseNotes        ((UBYTE *)0xA000)   /* SRAM bank 0, song +$0000 */
#define SramBookmarks          ((UBYTE *)0xAFF0)   /* SRAM bank 0, song +$0FF0 */
#define SramGrooves            ((UBYTE *)0xB090)   /* SRAM bank 0, song +$1090 */
#define SramSequence           ((UBYTE *)0xB290)   /* SRAM bank 0, song +$1290 */
#define SramTableEnvelopes     ((UBYTE *)0xB690)   /* SRAM bank 0, song +$1690 */
#define SramWords              ((UBYTE *)0xB890)   /* SRAM bank 0, song +$1890 */
#define SramWordNames          ((UBYTE *)0xBDD0)   /* SRAM bank 0, song +$1DD0 */
#define SramInitFlag1          ((UBYTE *)0xBE78)   /* SRAM bank 0, song +$1E78 */
#define SramInstrNames         ((UBYTE *)0xBE7A)   /* SRAM bank 0, song +$1E7A */
#define SramTableAlloc         ((UBYTE *)0xA020)   /* SRAM bank 1, song +$2020 */
#define SramInstrAlloc         ((UBYTE *)0xA040)   /* SRAM bank 1, song +$2040 */
#define SramChainPhrases       ((UBYTE *)0xA080)   /* SRAM bank 1, song +$2080 */
#define SramChainTransposes    ((UBYTE *)0xA880)   /* SRAM bank 1, song +$2880 */
#define SramInstruments        ((UBYTE *)0xB080)   /* SRAM bank 1, song +$3080 */
#define SramTableTransposes    ((UBYTE *)0xB480)   /* SRAM bank 1, song +$3480 */
#define SramTableCmd1          ((UBYTE *)0xB680)   /* SRAM bank 1, song +$3680 */
#define SramTableCmd2          ((UBYTE *)0xBA80)   /* SRAM bank 1, song +$3A80 */
#define SramInitFlag2          ((UBYTE *)0xBE80)   /* SRAM bank 1, song +$3E80 */
#define SramPhraseAlloc        ((UBYTE *)0xBE82)   /* SRAM bank 1, song +$3E82 */
#define SramChainAlloc         ((UBYTE *)0xBEA2)   /* SRAM bank 1, song +$3EA2 */
#define SramSynthParams        ((UBYTE *)0xBEB2)   /* SRAM bank 1, song +$3EB2 */
#define SramProperties         ((UBYTE *)0xBFB2)   /* SRAM bank 1, song +$3FB2 */
#define SramPropTempo          ((UBYTE *)0xBFB4)   /* SRAM bank 1, song +$3FB4 */
#define SramPropKeyDelay       ((UBYTE *)0xBFBA)   /* SRAM bank 1, song +$3FBA */
#define SramPropKeyRepeat      ((UBYTE *)0xBFBB)   /* SRAM bank 1, song +$3FBB */
#define SramPropPrelisten      ((UBYTE *)0xBFBC)   /* SRAM bank 1, song +$3FBC */
#define SramPropSyncMode       ((UBYTE *)0xBFBD)   /* SRAM bank 1, song +$3FBD */
#define SramPropColorSet       ((UBYTE *)0xBFBE)   /* SRAM bank 1, song +$3FBE */
#define SramPropSyncMode2      ((UBYTE *)0xBFBF)   /* SRAM bank 1, song +$3FBF */
#define SramPropCloneMode      ((UBYTE *)0xBFC0)   /* SRAM bank 1, song +$3FC0 */
#define SramPropActiveFile     ((UBYTE *)0xBFC1)   /* SRAM bank 1, song +$3FC1 */
#define SramPropTempoMode      ((UBYTE *)0xBFCC)   /* SRAM bank 1, song +$3FCC */
#define SramPhraseFx           ((UBYTE *)0xA000)   /* SRAM bank 2, song +$4000 */
#define SramPhraseFxVal        ((UBYTE *)0xAFF0)   /* SRAM bank 2, song +$4FF0 */
#define SramWaveFrames         ((UBYTE *)0xA000)   /* SRAM bank 3, song +$6000 */
#define SramPhraseInstruments  ((UBYTE *)0xB000)   /* SRAM bank 3, song +$7000 */
#define SramInitFlag3          ((UBYTE *)0xBFF0)   /* SRAM bank 3, song +$7FF0 */

/* --- bank 0 ---------------------------------------------------------------- */

/* runtime (hand-written asm) */
void DebugTrap(void);                                   /* `ld b,b` + rst $38 assert */
void FarCall(UBYTE bank, void (*fn)(void));
void FarCallArg(UBYTE bank, UWORD fn, UBYTE arg);
UBYTE FarCallByte(UBYTE bank, void (*fn)(void));    /* returns fn's byte result */
#define FAR_FN(f) ((void (*)(void))(UWORD)(f))

/* song data access */
UBYTE ReadSongByte(UBYTE bank, UBYTE *addr);
void WriteSongByte(UBYTE bank, UBYTE *addr, UBYTE val);
void ReadSongBytes(UBYTE bank, UBYTE *src, UBYTE *dst);  /* 16 bytes */
void WriteInstrByte(UBYTE instr, UBYTE field, UBYTE val);
UBYTE ReadInstrByte(UBYTE instr, UBYTE field);
UBYTE ReadCurInstrByte(UBYTE field);
UBYTE ReadSeqByte(UBYTE chan, UBYTE row);
void ReadKitName(UBYTE kit);                            /* -> wSampleNoteString */
UBYTE KitExists(UBYTE kit);

/* drawing */
void set_bkg_tiles(UBYTE x, UBYTE y, UBYTE w, UBYTE h, unsigned char *tiles);
void DrawString(char *s);
void DrawStringIndexed(UBYTE c);
void DrawStringTile20(void);
void DrawByte(UBYTE v);
void DrawHexByte(UBYTE v);
void DrawTempoField(void);
void GetTilemapAddr(UBYTE x, UBYTE y);                  /* sets wVramCursor (asm) */
void ClearMarkerSprites(void);
char *strcpy(char *dest, char *src);
void MemSet(UBYTE *dest, UBYTE value, UWORD count);
void memcpy(UBYTE *dst, UBYTE *src, UWORD n);

/* --- the screens' draw entry points (ScreenDrawDispatch, bank 0) ----------- */

void DrawPhraseScreen(void);        /* $04:$4CE5 */
void DrawGrooveScreen(void);        /* $1D:$41DA */
void DrawChainScreen(void);         /* $06:$5947 */
void DrawSongScreen(void);          /* $07:$4574 */
void DrawTableScreen(void);         /* $05:$4762 */
void DrawInstrumentScreen(void);    /* $03:$4DE7 */
void DrawWaveScreen(void);          /* $06:$4F18 */
void DrawProjectScreen(void);       /* $1B:$5C17 */
void DrawSynthScreen(void);         /* $1D:$68AB */
void DrawWordScreen(void);          /* $06:$41A2 */
void DrawFileScreen(void);          /* $1C:$57BB */
void DrawHelpScreen(void);          /* $1B:$4A62 */

/* --- bank 1 ---------------------------------------------------------------- */

void ShowMessage(char *text);
void DrawMessageBar(void);
void DrawTopBar(void);
void UiDrawTextHw(void);
void DrawOnOffName(UBYTE v);
void DrawSpeedModeName(UBYTE v);
void DrawSpeedGlyph(UBYTE v);
void CloneInstrTable(void);

/* --- bank 3: instrument screen --------------------------------------------- */

void WriteCurrentInstrByte(UBYTE field, UBYTE val);
void DrawInstrCursor(void);
void EraseInstrCursor(void);
UBYTE FindFreeSynth(void);
UBYTE SynthWavesModified(UBYTE synth);
void InstrTableModePrev(void);
UBYTE GetInstrByte2(void);
UBYTE GetInstrType3(void);
void WriteKitNumber(UBYTE field, UBYTE kit);
void InstrEnvelopeChanged(void);
void KitSelectEnsure(void);
void SetInstrLen2(void);
void InstrTableOn(void);
UBYTE IsInstrTableOn(void);
UBYTE GetInstrByte9(void);
UBYTE InstrByte9IsZero(void);
void SetInstrPitchByte(UBYTE v);
void InstrSelectPrevUsed(void);
UBYTE GetInstrByte10(void);
UBYTE GetInstrByte11(void);
UBYTE GetInstrFlags53(void);
void SetInstrFlags54(UBYTE v);

/* --- bank 5 ---------------------------------------------------------------- */

void MapDrawTopRow(void);

/* --- bank $1B: project screen ---------------------------------------------- */

void UpdateInstrEnvelope(void);
void DrawTextField(UBYTE a, UBYTE b, UBYTE c, UBYTE d);
UBYTE ProjSyncModeIs567(void);

/* --- bank $1F -------------------------------------------------------------- */

void DrawInstrTableField(void);
UBYTE GetInstrType1F(void);
UBYTE GetInstrEnv1F(void);
UBYTE FindFreeTable(void);
void GetInstrEnv1F2(void);

#endif /* LSDJ_H */
