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
void ReadKitSampleName(UBYTE kit, UBYTE sample); /* bank 0 */
extern char NoteNames[];                    /* $06:$76E4, [][3] */
extern char TxtKitNoteBad[];                /* $06:$7844 "???" */
extern char TxtKitNoteC5[];                 /* $06:$7830 "C 5" */
extern char TxtKitNoteC6[];                 /* $06:$7838 "C 6" */
extern char TxtKitNoteC7[];                 /* $06:$783C "C 7" */
extern char TxtKitNoteC8[];                 /* $06:$7840 "C 8" */
extern char TxtKitNoteF5[];                 /* $06:$7834 "F 5" */
extern char TxtNoNote[];                    /* $06:$782C "---" (this unit's literals follow NoteNames) */
extern char TxtNoteBad[];                   /* $06:$7848 "???" */
extern UBYTE wChannelDisplayNote[];         /* $C41F, [4] */
extern UBYTE wChannelDisplayNoteTimer[];    /* $C423, [4] */
extern UBYTE wChannelOutput[];              /* $C0A0, [4] */
extern UBYTE wChannelOutputOverride[];      /* $C0A4, [4] */
extern UBYTE wKit1Bank;                     /* $C4F4 */
extern UBYTE wKit2Bank;                     /* $C4F5 */
extern UBYTE wNoteTranspose;                /* $C4D3 */
extern UBYTE wSoundUpdateFlags;             /* $C8F5 */
extern UBYTE wTempoShadow;                  /* $C947 */
void DrawTileHexCell(UBYTE v);              /* c/bank06/DrawTileHexCell.c */
void DrawNoteName(UBYTE column, UBYTE v);
void DrawStatusNote(UBYTE channel, UBYTE note);
void UpdateScope(void);

/* $71ED: draw channel ch's playing note in the status panel at (17, ch + 6):
   a kit's sample name on the wave channel, wNoiNote on the noise channel,
   else DrawNoteName (PU2 shifted by wNoteTranspose) */
void DrawStatusNote(UBYTE channel, UBYTE note)
{
    UBYTE c, n;

    SetVramCursor(17, channel + 6);
    if (wSoundUpdateFlags && channel == 2) {
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
    switch (channel) {
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
        DrawNoteName(channel, note);
}

/* $72C4: per-frame status update for most screens: pending sample-name cells
   (wChannelDisplayNote) at x=17, the tempo field when wTempo changed, the
   scope */
void UpdateStatusPanel(void)
{
    UBYTE channel;
    UBYTE n;
    UBYTE *p;
    UBYTE t;

    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    for (channel = 0; channel < 4; channel++) {
        t = (wChannelOutput[channel] & 0x80) && !wChannelOutputOverride[channel];
        if (t)
            continue;
        n = wChannelDisplayNote[channel];
        if (n) {
            DrawStatusNote(channel, n);
            wChannelDisplayNote[channel] = 0;
            if (channel == 2)
                PositionWavSprites();
            wChannelDisplayNoteTimer[channel] = 0x19;
        } else {
            p = &wChannelDisplayNoteTimer[channel];
            n = *p;
            if (n != 0) {
                if (n == 1) {
                    SetVramCursor(17, channel + 6);
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

/* $73C0: draw string s in the status panel's sync slot ($99B0, palette 4/1) */
void DrawSyncStatusText(char *s)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(16, 13);
    DrawString(s);
}

/* $740C: sync mode LSDJ: WAIT while handshaking, LEAD while running, else
   SYNC or blank (SyncStatusFlag) */
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

/* $7469: sync mode MIDI: SYNC (SyncStatusFlag), WAIT while playing, else
   blank */
void DrawSyncStatusMidi(void)
{
    if (SyncStatusFlag())
        DrawSyncStatusText("SYNC");
    else if (wPlayMode)
        DrawSyncStatusText("WAIT");
    else
        DrawSyncStatusText("    ");
}

/* $74AD: draw the sync/lead status by wSyncMode (1/3/6/7 cases) */
void DrawSyncStatus(void)
{
    switch (wSyncMode) {
    case SYNC_LSDJ:
        DrawSyncStatusLsdj();
        break;
    case SYNC_MIDI:
        DrawSyncStatusMidi();
        break;
    case SYNC_ANALOG_IN:
        DrawSyncStatusText(wPlayMode ? "SYNC" : "    ");
        break;
    case SYNC_ANALOG_OUT:
        DrawSyncStatusText(wPlayMode ? "LEAD" : "    ");
        break;
    }
}

/* $75BA: draw note `v` for channel `column`: "---" for none; on the noise
   channel a hex value below $3C, else a kit note (C 5..C 8); else a
   NoteNames entry, the wave channel an octave down; "???" past the table */
void DrawNoteName(UBYTE column, UBYTE v)
{
    char *p;
    UBYTE t;

    if (!v) {
        DrawString(TxtNoNote);
    } else if (column == 3) {
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
        DrawStringIndexed(column == 2 ? p[2] - 1 : p[2]);
    } else {
        DrawString(TxtNoteBad);
    }
}
