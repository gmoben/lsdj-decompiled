/*
 * Sync and transport helpers -- LSDj 9.4.0, ROM bank $00:$1CFF-$24A7.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The sync setting and transport (start/stop, the per-mode clock tick, the
 * analog clock out, the received-byte handler), the CPU-overload message and
 * the unchecked sequence/chain readers the song-position sync uses
 * (ReadSeqByte's twins without the asserts). SyncStartTransport's guard is
 * an early `return` (`if (!lsdj && wSyncMode != 3) return;`), which is what
 * leaves the ROM's `jp z,body; jp end`.
 */

#include "lsdj.h"

extern volatile sfr UBYTE SC_REG;           /* $FF02 */
extern volatile sfr UBYTE hSoundCh;         /* $FF8B, the sound engine's channel */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChanNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChanStarted[];                /* $C304, [4] */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wLiveStartCol;                 /* $C571 */
extern UBYTE wAnalogOutTimer;               /* $CB68 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncHandshake;                /* $CB6C */
extern UBYTE wSyncSendGate;                 /* $CB6E */
extern UBYTE wSyncRxPos;                    /* $CB71 */
extern UBYTE wSyncTxByte;                   /* $CBDC */
extern UBYTE wSyncRxByte;                   /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
extern char TxtTooBusy[];                   /* $01:$6FAE, "TOO BUSY!" */

void SndInitPlayState(void);                /* bank 2 */
void SetInterruptEnable(UBYTE ie);          /* asm */
void SendSyncHandshake(void);
void SendSyncByte(void);                    /* asm */
void SendByteStep(void);
void SyncApplyMode(void);
void SyncTickLSDJ(void);
void SyncSetSendMode(void);
void SyncSetRecvMode(void);
void StopPlayback(void);                    /* bank 2 */
void SndSetupChanState5F5E(void);           /* bank 2 */
void EnableSerialSync(void);                /* bank $1C */

void LoadSyncSetting(void)
{
    wSyncMode = *SramPropSyncMode;
    wSyncMode2 = *SramPropSyncMode2;
    if (wSyncMode > 7)
        wSyncMode = 0;
    if (!wSyncMode2)
        wSyncMode2 = 6;
    SyncApplyMode();
}

void SyncStartTransport(void)
{
    UBYTE lsdj;

    lsdj = wSyncMode == 1;
    if (!lsdj && wSyncMode != 3)
        return;
    if (lsdj && wSyncTick == 0xFF) {
        wSyncRun = 1;
        SyncSetSendMode();
    } else {
        wSyncHandshake = 1;
        SendSyncHandshake();
    }
}

void SyncStopTransport(void)
{
    switch (wSyncMode) {
    case 1:
    case 3:
        wSyncRun = 0;
        wSyncTick = 0xFF;
        wSyncHandshake = 0;
        SyncSetRecvMode();
        break;
    case 7:
        wAnalogOutTimer = 3;
        break;
    }
}

void SyncTickLSDJ(void)
{
    UBYTE *saved;

    if (wLiveMode) {
        saved = wVramCursor;
        for (hSoundCh = 0; hSoundCh < 4; hSoundCh++) {
            wChanNextSongRow[hSoundCh] = 0xFF;
            wChanStarted[hSoundCh] = 0;
            wChanActive[hSoundCh] = 0;
            _asm di _endasm;
            SWITCH_ROM(2);
            SndSetupChanState5F5E();
            _asm ei _endasm;
            if (wCurrentScreen == 4) {
                GetTilemapAddr(hSoundCh * 3 + 2, (wChanSongRow[hSoundCh] & 0x0F) + 2);
                DrawStringTile20();
            }
        }
        wVramCursor = (UBYTE *)0x99B0;
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("    ");
        wVramCursor = saved;
    } else {
        wPlayMode = wCurrentScreen;
        _asm di _endasm;
        SWITCH_ROM(2);
        StopPlayback();
        _asm ei _endasm;
    }
}

void SyncTickMidi(void)
{
    if (wLiveMode) {
        SyncTickLSDJ();
    } else {
        SWITCH_ROM(2);
        StopPlayback();
        StopPlayback();
    }
}

void DispatchSyncClock(void)
{
    UBYTE saved;

    saved = hCurrentBank;
    wSyncTick = 0xFF;
    if (wPlayMode) {
        if (wSyncMode == 1)
            SyncTickLSDJ();
        else if (wSyncMode == 3)
            SyncTickMidi();
    }
    SWITCH_ROM(saved);
}

void SyncSetSendMode(void)
{
    wSyncSerialIeMask = 0;
    wSyncSendGate = 1;
    SetInterruptEnable((IE_REG & 4) | 1);
}

void SyncSetRecvMode(void)
{
    wSyncSerialIeMask = 8;
    wSyncSendGate = 0;
    SetInterruptEnable((IE_REG & 4) | 9);
    wSyncTick = 0xFF;
    SendSyncHandshake();
}

void SyncApplyMode(void)
{
    _asm di _endasm;
    wSyncRun = 0;
    switch (wSyncMode) {
    case 0:
    case 7:
        SyncSetSendMode();
        wSyncTick = 0xFF;
        break;
    case 6:
        wSyncSerialIeMask = 0;
        wSyncSendGate = 2;
        SetInterruptEnable(5);
        wSyncTick = 0xFF;
        break;
    case 1:
        if (wPlayMode) {
            wSyncRun = 1;
            SyncSetSendMode();
        } else
            SyncSetRecvMode();
        break;
    case 3:
        SyncSetRecvMode();
        break;
    case 5:
        wSyncSerialIeMask = 8;
        wSyncSendGate = 1;
        if (wSyncState != 2) {
            SetInterruptEnable((IE_REG & 4) | 9);
            SendSyncHandshake();
        }
        break;
    }
    _asm ei _endasm;
}

void SyncSendAnalogClock(void)
{
    if ((SC_REG & 0x81) == 0x81)
        DebugTrap();
    wSyncTxByte = wAnalogOutTimer < 3 ? 0xFF : 0x00;
    SendSyncByte();
    if (wAnalogOutTimer == 0)
        wAnalogOutTimer = wSyncMode2;
    wAnalogOutTimer--;
}

void HandleTooBusy(void)
{
    UBYTE saved;

    saved = hCurrentBank;
    SWITCH_ROM(2);
    SndInitPlayState();
    SWITCH_ROM(1);
    ShowMessage(TxtTooBusy);
    SWITCH_ROM(saved);
}

UBYTE ReadSeqByteRaw(UBYTE channel, UBYTE step)
{
    UBYTE *p;

    p = SramSequence + ((unsigned int)step << 2);
    return ReadSongByteRaw(0, p + channel);
}

UBYTE ReadChainByte(UBYTE chain, UBYTE row)
{
    UBYTE *p;

    p = SramChainPhrases + ((unsigned int)chain << 4);
    return ReadSongByteRaw(1, p + row);
}

void ProcessSyncByte(void)
{
    UBYTE saved;

    if (wSyncMode == 1 && !wSyncRun && wLiveStartCol == 0xFF) {
        if (wSyncRxByte < 0xFD) {
            wSyncRxPos = wSyncRxByte;
        } else if (wPlayMode) {
            if (wSyncHandshake) {
                if (wSyncRxByte > 0xFD) {
                    SendSyncHandshake();
                    return;
                }
                wSyncHandshake = 0;
            }
        } else if (wSyncRxByte == 0xFE) {
            saved = hCurrentBank;
            SWITCH_ROM(0x1C);
            EnableSerialSync();
            SWITCH_ROM(saved);
            return;
        }
        SendSyncHandshake();
    }
    SendByteStep();
}
