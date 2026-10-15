/*
 * The status panel -- LSDj 9.4.0, ROM bank $06.
 *
 * Updated every frame on most screens: each channel's playing note (a kit's
 * sample name or a note name) and the sync status.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncHandshake;                /* $CB6C */
void DrawSyncStatusText(char *s);
void DrawSyncStatusLsdj(void);
void DrawSyncStatusMidi(void);
UBYTE SyncStatusFlag(void);
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */
extern char NoteNames[];                    /* $06:$76E4, [][3] */
extern char TxtKitNoteBad[];                /* $06:$7844 "???" */
extern char TxtKitNoteC5[];                 /* $06:$7830 "C 5" */
extern char TxtKitNoteC6[];                 /* $06:$7838 "C 6" */
extern char TxtKitNoteC7[];                 /* $06:$783C "C 7" */
extern char TxtKitNoteC8[];                 /* $06:$7840 "C 8" */
extern char TxtKitNoteF5[];                 /* $06:$7834 "F 5" */
extern char TxtNoNote[];                    /* $06:$782C "---" (this unit's literals follow NoteNames) */
extern char TxtNoteBad[];                   /* $06:$7848 "???" */
extern UBYTE wChanDispNote[];               /* $C41F, [4] */
extern UBYTE wChanDispNoteTimer[];          /* $C423, [4] */
extern UBYTE wChanOutput[];                /* $C0A0, [4] */
extern UBYTE wChanOutputOverride[];                /* $C0A4, [4] */
extern UBYTE wKit1Bank;                     /* $C4F4 */
extern UBYTE wKit2Bank;                     /* $C4F5 */
extern UBYTE wNoteTranspose;                /* $C4D3 */
extern UBYTE wSoundUpdateFlags;               /* $C8F5 */
extern UBYTE wTempoShadow;                  /* $C947 */
void DrawTileHexCell(UBYTE v);                /* c/bank06/DrawTileHexCell.c */
void DrawNoteName(UBYTE col, UBYTE v);
void DrawStatusNote(UBYTE chan, UBYTE note);
void UpdateScope(void);

void DrawStatusNote(UBYTE chan, UBYTE note)
{
    UBYTE c, n;

    SetVramCursor(17, chan + 6);
    if (wSoundUpdateFlags && chan == 2) {
        wSampleNoteString[0] = 0;
        if (note & 0xF0)
            ReadKitSampleName(wKit1Bank, note >> 4);
        else
            ReadKitSampleName(wKit2Bank, note & 0x0F);
        c = wSampleNoteString[1];
        if (c == '-')
            wSampleNoteString[1] = ' ';
        c = wSampleNoteString[2];
        if (c == '-')
            wSampleNoteString[2] = ' ';
        DrawString(wSampleNoteString);
        return;
    }
    switch (chan) {
    case 3:
        n = *(UBYTE *)0xC2E7;               /* wNoiNote, read through the literal */
        n++;
        DrawNoteName(3, n);
        return;
    case 1:
        note += wNoteTranspose;
        break;
    }
    if (note <= 0x6C)
        DrawNoteName(chan, note);
}

void UpdateStatusPanel(void)
{
    UBYTE i;
    UBYTE n;
    UBYTE *p;
    UBYTE t;

    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    for (i = 0; i < 4; i++) {
        t = (wChanOutput[i] & 0x80) && !wChanOutputOverride[i];
        if (t)
            continue;
        n = wChanDispNote[i];
        if (n) {
            DrawStatusNote(i, n);
            wChanDispNote[i] = 0;
            if (i == 2)
                PositionWavSprites();
            wChanDispNoteTimer[i] = 0x19;
        } else {
            p = &wChanDispNoteTimer[i];
            n = *p;
            if (n != 0) {
                if (n == 1) {
                    SetVramCursor(17, i + 6);
                    DrawString("   ");
                }
                *p = n - 1;
            }
        }
    }
    if (wTempo != wTempoShadow) {
        wTempoShadow = wTempo;
        DrawTempoField();
    }
    UpdateScope();
}

void DrawSyncStatusText(char *s)
{
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x99B0;
    DrawString(s);
}

void DrawSyncStatusLsdj(void)
{
    if (wSyncHandshake)
        DrawSyncStatusText("WAIT");
    else if (wSyncRun)
        DrawSyncStatusText("LEAD");
    else if (SyncStatusFlag())
        DrawSyncStatusText("SYNC");
    else
        DrawSyncStatusText("    ");
}

void DrawSyncStatusMidi(void)
{
    if (SyncStatusFlag())
        DrawSyncStatusText("SYNC");
    else if (wPlayMode)
        DrawSyncStatusText("WAIT");
    else
        DrawSyncStatusText("    ");
}

void DrawSyncStatus(void)
{
    switch (wSyncMode) {
    case 1:
        DrawSyncStatusLsdj();
        break;
    case 3:
        DrawSyncStatusMidi();
        break;
    case 6:
        DrawSyncStatusText(wPlayMode ? "SYNC" : "    ");
        break;
    case 7:
        DrawSyncStatusText(wPlayMode ? "LEAD" : "    ");
        break;
    }
}

void DrawNoteName(UBYTE col, UBYTE v)
{
    char *p;
    UBYTE t;

    if (!v) {
        DrawString(TxtNoNote);
    } else if (col == 3) {
        t = v - 1;
        if (t < 0x3C) {
            DrawHexByte(t);
            DrawStringTile20();
        } else if ((UWORD)t < 0x78) {
            switch (t) {
            default:
                DrawTileHexCell(t);
                break;
            case 0x73:
                DrawString(TxtKitNoteC5);
                break;
            case 0x74:
                DrawString(TxtKitNoteF5);
                break;
            case 0x75:
                DrawString(TxtKitNoteC6);
                break;
            case 0x76:
                DrawString(TxtKitNoteC7);
                break;
            case 0x77:
                DrawString(TxtKitNoteC8);
                break;
            }
        } else {
            DrawString(TxtKitNoteBad);
        }
    } else if (v <= 0x6C) {
        p = NoteNames + (UWORD)v * 3;
        DrawStringIndexed(p[0]);
        DrawStringIndexed(p[1]);
        DrawStringIndexed(col == 2 ? p[2] - 1 : p[2]);
    } else {
        DrawString(TxtNoteBad);
    }
}
