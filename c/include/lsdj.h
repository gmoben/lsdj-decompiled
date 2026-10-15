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
 *     right, an extern's address it does not (NextUsedInstrument tells them
 *     apart), so the SramXxx names below are literal-pointer macros.
 *   - Banked calls go through FarCall/FarCallArgument (bank 0, hand-written asm).
 *     Most call sites pass the function itself (`ld bc,#fn`); others pass
 *     it cast through an integer, which SDCC loads byte-wise (`ld c,#<fn;
 *     ld b,#>fn`) -- FAR_FN(fn) reproduces those. FarCallArgument's address
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
extern volatile UBYTE rROMB0;               /* $2000, the MBC5 ROM bank register */
/* volatile: SoundSpeechStep's `rRAMG = 0x0A; x = *p; rRAMG = 0;` keeps both
 * stores, which SDCC drops as dead for a plain global */
extern volatile UBYTE rRAMG;                /* $0000, MBC5 SRAM enable ($0A on) */
extern volatile UBYTE rRAMB;                /* $4000, MBC5 SRAM bank */

/* GBDK's SWITCH_ROM_MBC1 is `*(UBYTE *)0x2000 = n`, which SDCC compiles through
 * de (`ld de,#0x2000; ld a,#n; ld (de),a`); the ROM has the named-variable form
 * (`ld hl,#0x2000; ld (hl),#n`), so LSDj wrote the register as a variable. It
 * also keeps the HRAM shadow the far-call helpers rely on. */
#define SWITCH_ROM(n) hCurrentBank = (n); rROMB0 = (n)

/* --- WRAM ----------------------------------------------------------------- */

extern UBYTE wCurrentScreen;                /* $C401 */
extern UBYTE wIsCgb;                        /* $C402 */
extern UBYTE wButtonsSame;                  /* $C408: 1 if the buttons are unchanged since last frame */
extern UBYTE wSelectionLeft;                      /* $C40D */
extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wPhrase;                       /* $C414 */
extern UBYTE wPhraseRow;                    /* $C416 */
extern UBYTE wSongColumn;                      /* $C41D: song cursor column (channel) */
extern UBYTE wSongRow;                      /* $C41E: song cursor row */
extern UBYTE wInstrumentField;                   /* $C427: instrument screen cursor row */
extern UBYTE wSpeechRow;              /* $C429 */
extern char wSampleNoteString[];            /* $C43A: [7] shared name buffer */
extern UBYTE wSongReadBuffer[];                /* $C46D, [64] the song window (16 rows x 4 channels) */
extern UBYTE wUsedTableMap[];               /* $C47D, [32] tables referenced by instruments; also a scratch buffer */
extern UBYTE wPlayMode;                     /* $C4D2 */
extern UBYTE wTempo;                        /* $C529, 0-39 stand for 256-295 */
extern UBYTE wTempoMode;                    /* $C52A: 0 = BPM, 1-3 = the 2X/3X/6X sync speeds */
extern UBYTE wShadedDigitPalette;               /* $C566 */
extern UBYTE wShadedPalette;                    /* $C567 */
extern UBYTE wHighlightDigitPalette;               /* $C568 */
extern UBYTE wHighlightPalette;                    /* $C569 */
extern UBYTE *wStringSource;                   /* $C564, the character-to-tile map DrawString reads */
extern UBYTE AsciiCharacterMap[];                /* $0:$086F, text from ' '; wStringSource = AsciiCharacterMap - 0x20 */
extern UBYTE IdentityCharacterMap[];             /* $0:$08C8, raw tile indices */
extern UBYTE *wVramCursor;                  /* $C573 */
extern UBYTE wInstrument;                   /* $C576, the instrument being edited */
extern UBYTE wStringBuffer[];                  /* $C6A9, 20 chars + NUL */
extern UBYTE wMessageTimer;                     /* $C7A9 */
extern UBYTE *wMessageStringPointer;                   /* $C7AA */
extern UBYTE wMessagePanelShown;                /* $C7AC */
extern UBYTE wInstrumentEditLatch;               /* $C906: instrument screen edit latch */
extern char *wChainScratchPointer;              /* $C948 */
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
#define SramInitializedMarker1          ((UBYTE *)0xBE78)   /* SRAM bank 0, song +$1E78 */
#define SramInstrumentNames         ((UBYTE *)0xBE7A)   /* SRAM bank 0, song +$1E7A */
#define SramTableAllocation         ((UBYTE *)0xA020)   /* SRAM bank 1, song +$2020 */
#define SramInstrumentAllocation         ((UBYTE *)0xA040)   /* SRAM bank 1, song +$2040 */
#define SramChainPhrases       ((UBYTE *)0xA080)   /* SRAM bank 1, song +$2080 */
#define SramChainTransposes    ((UBYTE *)0xA880)   /* SRAM bank 1, song +$2880 */
#define SramInstruments        ((UBYTE *)0xB080)   /* SRAM bank 1, song +$3080 */
#define SramTableTransposes    ((UBYTE *)0xB480)   /* SRAM bank 1, song +$3480 */
#define SramTableCommands1          ((UBYTE *)0xB680)   /* SRAM bank 1, song +$3680 */
#define SramTableCommands2          ((UBYTE *)0xBA80)   /* SRAM bank 1, song +$3A80 */
#define SramInitializedMarker2          ((UBYTE *)0xBE80)   /* SRAM bank 1, song +$3E80 */
#define SramPhraseAllocation        ((UBYTE *)0xBE82)   /* SRAM bank 1, song +$3E82 */
#define SramChainAllocation         ((UBYTE *)0xBEA2)   /* SRAM bank 1, song +$3EA2 */
#define SramSynthParameters        ((UBYTE *)0xBEB2)   /* SRAM bank 1, song +$3EB2 */
#define SramProperties         ((UBYTE *)0xBFB2)   /* SRAM bank 1, song +$3FB2 */
#define SramPropertyTempo          ((UBYTE *)0xBFB4)   /* SRAM bank 1, song +$3FB4 */
#define SramPropertyKeyDelay       ((UBYTE *)0xBFBA)   /* SRAM bank 1, song +$3FBA */
#define SramPropertyKeyRepeat      ((UBYTE *)0xBFBB)   /* SRAM bank 1, song +$3FBB */
#define SramPropertyPrelisten      ((UBYTE *)0xBFBC)   /* SRAM bank 1, song +$3FBC */
#define SramPropertySyncMode       ((UBYTE *)0xBFBD)   /* SRAM bank 1, song +$3FBD */
#define SramPropertyColorSet       ((UBYTE *)0xBFBE)   /* SRAM bank 1, song +$3FBE */
#define SramPropertySyncMode2      ((UBYTE *)0xBFBF)   /* SRAM bank 1, song +$3FBF */
#define SramPropertyCloneMode      ((UBYTE *)0xBFC0)   /* SRAM bank 1, song +$3FC0 */
#define SramPropertyActiveFile     ((UBYTE *)0xBFC1)   /* SRAM bank 1, song +$3FC1 */
#define SramPropertyTempoMode      ((UBYTE *)0xBFCC)   /* SRAM bank 1, song +$3FCC */
#define SramPhraseCommands           ((UBYTE *)0xA000)   /* SRAM bank 2, song +$4000 */
#define SramPhraseCommandValues        ((UBYTE *)0xAFF0)   /* SRAM bank 2, song +$4FF0 */
#define SramWaveFrames         ((UBYTE *)0xA000)   /* SRAM bank 3, song +$6000 */
#define SramPhraseInstruments  ((UBYTE *)0xB000)   /* SRAM bank 3, song +$7000 */
#define SramInitializedMarker3          ((UBYTE *)0xBFF0)   /* SRAM bank 3, song +$7FF0 */

/* --- bank 2: the sound engine ----------------------------------------------- */

/* Far-call targets, declared as the banked-call routines take them; bank 2's
   own units (SOUND_BANK) define them with their real types, and a unit that
   calls one directly with bank 2 paged in (SND_DIRECT: Main.c's
   ProcessInput) declares it so itself. */
#if !defined(SOUND_BANK) && !defined(SND_DIRECT)
void SoundGetChainPhrase(void);               /* $02:$6F65 (far): chain_phrases[chain][row] */
#endif

/* --- bank 0 ---------------------------------------------------------------- */

/* runtime (hand-written asm) */
void DebugTrap(void);                                   /* `ld b,b` + rst $38 assert */
void FarCall(UBYTE bank, void (*function)(void));
void FarCallArgument(UBYTE bank, void (*function)(void), UBYTE argument);    /* FAR_FN(f) for the byte-wise form */

/* far calls with arguments or a byte result (C: c/bank00/FarCall.c) */
UBYTE FarCallArgumentByte(UBYTE bank, UWORD function, UBYTE argument);         /* returns fn's byte */
UBYTE FarCallArgument2(UBYTE bank, UWORD function, UBYTE a, UBYTE b);
UBYTE FarCallArgument2P(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b);  /* the same routine taking FAR_FN(f) (match_function.py aliases it) */
void FarCallArgument3(UBYTE bank, UWORD function, UBYTE a, UBYTE b, UBYTE c);
void FarCallArgument3P(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b, UBYTE c);  /* the same routine taking FAR_FN(f) (match_function.py aliases it) */
UBYTE FarCallByteU(UBYTE bank, UBYTE (*function)(void));  /* FarCallByte for a callee declared returning UBYTE: `ld bc,#f` (aliased) */
#ifndef FARCALL_C
/* their callers pass a function; FarCall.c defines them taking a UWORD */
void FarCallArgument2V(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b);
UBYTE FarCallByte(UBYTE bank, void (*function)(void));    /* returns fn's byte result */
#endif
#define FAR_FN(f) ((void (*)(void))(UWORD)(f))

/* song data access */
#ifdef SONG_BYTE_PTR
UBYTE ReadSongByte(UBYTE bank, UBYTE *address);    /* bank 3 declares the address a pointer */
#else
UBYTE ReadSongByte(UBYTE bank, UWORD address);
#endif
void WriteSongByte(UBYTE bank, UWORD address, UBYTE value);
UBYTE ReadSongByteRaw(UBYTE bank, UWORD address);  /* no range asserts */
void ReadSongBytes(UBYTE bank, UBYTE *source, UBYTE *destination);  /* 16 bytes */
void WriteInstrumentByte(UBYTE instrument, UBYTE field, UBYTE value);
UBYTE ReadInstrumentByte(UBYTE instrument, UBYTE field);
UBYTE ReadCurrentInstrumentByte(UBYTE field);
UBYTE ReadSequenceByte(UBYTE channel, UBYTE row);
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
UWORD GetTempoTimerValue(void);                 /* the tempo as shown (or a speed code) */
void DrawTempo(UWORD tempo);
#ifndef GBDKLIB_C       /* GbdkLib.c defines it with `...` */
void printf(char *fmt, UWORD v);            /* printf-style, varargs */
#endif
void printf2(char *fmt, UWORD a, UWORD b);  /* the same routine with two values (match_function.py aliases it) */
void SetVramCursor(UBYTE x, UBYTE y);                  /* sets wVramCursor (asm) */
void ClearMarkerSprites(void);
char *strcpy(char *dest, char *source);
void FillMemory(UBYTE *dest, UBYTE value, UWORD count);
void memcpy(UBYTE *destination, UBYTE *source, UWORD n);

/* misc */
void SortBytePair(UBYTE *a, UBYTE *b);               /* swap so *a <= *b */
UBYTE ReadClipboardCell(UBYTE column, UBYTE row);         /* wClipboardBuffer[col*42 + row] */
void WriteClipboardCell(UBYTE column, UBYTE row, UBYTE v);
void LoadSynthShape(char *source);              /* bank $1E seed bytes -> wSynthShapeBuffer */

/* --- the screens' draw entry points (DispatchScreenDraw, bank 0) ----------- */

void RedrawScreen(void);                    /* $24DD: full repaint of the current screen */
void ClearBgTilemap(void);                  /* $17FB: tile 0 over the visible BG map */
extern UBYTE CommandMnemonics[];                 /* $0824: "-ABCDEFGHKLMOPRSTVWZ", the command letters */
void SetSpriteY(UBYTE sprite, UBYTE row);   /* play-position marker sprites */
void ClearSpriteY(UBYTE sprite);
void SetSpriteX(UBYTE sprite, UBYTE column);
void SetSpriteTile3(UBYTE sprite);
void SetSpriteTile4(UBYTE sprite);
void SetSpriteAttribute(UBYTE sprite, UBYTE attribute);
void SetOamByte(UWORD offset, UBYTE v);         /* wOamBuffer[offset] = v if changed */
void ClearSprites012(void);
void PositionWavSprites(void);

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
void DrawScreenPaletteMap(void);
void DrawOnOffName(UBYTE v);
void DrawSpeedModeName(UBYTE v);
void DrawSpeedGlyph(UBYTE v);
void CloneInstrumentTable(void);

/* --- bank 3: instrument screen --------------------------------------------- */

void WriteCurrentInstrumentByte(UBYTE field, UBYTE value);
void DrawInstrumentCursor(void);
void EraseInstrumentCursor(void);
UBYTE FindFreeSynth(void);
UBYTE SynthWavesModified(UBYTE synth);
void PreviousInstrumentTableMode(void);
UBYTE GetWaveLoopPosition(void);
UBYTE GetInstrumentType3(void);
void WriteKitNumber(UBYTE field, UBYTE kit);
void InstrumentEnvelopeChanged(void);
void EnsureKitExists(void);
void CloneSynth(void);
void SetInstrumentTableOn(void);
UBYTE IsInstrumentTableOn(void);
UBYTE GetWavePlayMode(void);
UBYTE WavePlayIsManual(void);
void SetInstrumentPitchByte(UBYTE v);
void PreviousUsedInstrument(void);
UBYTE GetWaveLength(void);
UBYTE GetWaveSpeed(void);
UBYTE GetVibratoShape(void);
void SetVibratoShape(char v);               /* the shape byte is signed here: its OR reads it through a cast */

/* --- bank 5 ---------------------------------------------------------------- */
void RestoreSelectionState(void);           /* $05:$747D (far) */

void DrawMapTopRow(void);

/* --- bank $1B: project screen ---------------------------------------------- */

void UpdateInstrumentEnvelope(void);
void ColorField(UBYTE a, UBYTE b, UBYTE c, UBYTE d);
UBYTE ProjectSyncModeIs567(void);

/* --- bank $1F -------------------------------------------------------------- */

void DrawInstrumentTableField(void);
void DrawInstrumentTableMode(void);
UBYTE InstrumentEnvelopeHasSecond(void);
UBYTE InstrumentEnvelopeHasThird(void);
UBYTE FindFreeTable(void);
void DrawEnvelopeFields(void);

#endif /* LSDJ_H */
