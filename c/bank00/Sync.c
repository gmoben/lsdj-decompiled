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
 * leaves the ROM's `jp z,body; jp end`. SyncTickLsdj needs track_hram
 * (hSoundChannel reused across its loop test); SendByteStep is the re-entrancy
 * guarded step loop the serial interrupt and the analog clock drive.
 * CheckRomChecksum (the cartridge test's ROM pass) sits between them: its
 * low-byte test is SDCC's `cp a,a` miscompile, so it always passes.
 */

#include "lsdj.h"

extern volatile sfr UBYTE SB_REG;           /* $FF01 */
extern volatile sfr UBYTE SC_REG;           /* $FF02 */
extern volatile sfr UBYTE DIV_REG;          /* $FF04 */
extern volatile sfr UBYTE NR50_REG;         /* $FF24 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE hSoundChannel;         /* $FF8B, the sound engine's channel */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wChannelPhrase[];                 /* $C168, [4] */
extern UBYTE wChannelChain[];                  /* $C170, [4] */
extern UBYTE wChannelChainRow[];               /* $C17C, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelStopQueued[];                /* $C304, [4] */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wPlayMinutes;                 /* $C552 */
extern UBYTE wPlaySeconds;                 /* $C553 */
extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UBYTE wAnalogOutTimer;               /* $CB68 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wSyncStartRow;                 /* $CB6A */
extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncHandshake;                /* $CB6C */
extern UBYTE wSyncSendGate;                 /* $CB6E */
extern UBYTE wSyncRxPos;                    /* $CB71 */
extern UBYTE wSendByteNest;                 /* $CB72 */
extern UWORD wAnaInStepLen;                 /* $CB73 */
extern UWORD wAnaInStepAccum;               /* $CB75 */
extern UBYTE wSyncInPulses;                 /* $CB77 */
extern UBYTE wSyncInGate;                   /* $CB78 */
extern UBYTE wAnaInPulseCount;              /* $CB79 */
extern UBYTE wAnaInPrevPlayMode;            /* $CB7A */
extern UBYTE wSyncTxByte;                   /* $CBDC */
extern UBYTE wSyncRxByte;                   /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
extern char TxtTooBusy[];                   /* $01:$6FAE, "TOO BUSY!" */

void SoundInitPlayState(void);                /* bank 2 */
void SetInterruptEnable(UBYTE ie);          /* asm */
void SendSyncHandshake(void);
void SendSyncByte(void);                    /* asm */
UWORD ChecksumRomBank(void);                /* $00:$20B3, asm */
void SendByteStep(void);
void SyncApplyMode(void);
void SyncTickLsdj(void);
void SyncSetSendMode(void);
void SyncSetRecvMode(void);
void SoundTogglePlayback(void);                    /* bank 2 */
void SoundResetChannelCommand(void);           /* bank 2 */
void SoundResetCommandState(void);                 /* bank 2 */
void SoundEnableLcdInts(void);                /* bank 2 */
void SoundRunFrame(void);                     /* bank 2 */
void DrawSyncSubSetting(void);              /* bank $1B */
void ReceiveLinkClipboard(void);                /* bank $1C */

void SyncLoadSetting(void)
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

void SyncTickLsdj(void)
{
    UBYTE *saved;

    if (wLiveMode) {
        saved = wVramCursor;
        for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++) {
            wChannelNextSongRow[hSoundChannel] = 0xFF;
            wChannelStopQueued[hSoundChannel] = 0;
            wChannelActive[hSoundChannel] = 0;
            _asm di _endasm;
            SWITCH_ROM(2);
            SoundResetChannelCommand();
            _asm ei _endasm;
            if (wCurrentScreen == 4) {
                SetVramCursor(hSoundChannel * 3 + 2, (wChannelSongRow[hSoundChannel] & 0x0F) + 2);
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
        SoundTogglePlayback();
        _asm ei _endasm;
    }
}

void SyncTickMidi(void)
{
    if (wLiveMode) {
        SyncTickLsdj();
    } else {
        SWITCH_ROM(2);
        SoundTogglePlayback();
        SoundTogglePlayback();
    }
}

void DispatchSyncClock(void)
{
    UBYTE saved;

    saved = hCurrentBank;
    wSyncTick = 0xFF;
    if (wPlayMode) {
        if (wSyncMode == 1)
            SyncTickLsdj();
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

UBYTE CheckRomChecksum(void)
{
    UBYTE bank;
    UBYTE lo;
    volatile UBYTE hi;      /* (UWORD)hi is widened afresh for the last test */
    UWORD sum;
    UWORD p;

    lo = *(UBYTE *)0x014F;  /* HeaderChecksumLo */
    hi = *(UBYTE *)0x014E;  /* HeaderChecksumHi */
    sum = 0;
    for (p = 0; p < 0x4000; p++)
        sum += *(UBYTE *)p;
    for (bank = 1; bank < 0x40; bank++) {
        *(UBYTE *)0x2000 = bank;
        sum += ChecksumRomBank();
    }
    sum -= hi;
    sum -= lo;
    /* SDCC compiles the low-byte test to `cp a,a` (lo fetched into a over
       the byte it is compared with), so only the high byte is checked */
    return (sum & 0xFF) == lo && (sum >> 8) == hi;
}

void HandleTooBusy(void)
{
    UBYTE saved;

    saved = hCurrentBank;
    SWITCH_ROM(2);
    SoundInitPlayState();
    SWITCH_ROM(1);
    ShowMessage(TxtTooBusy);
    SWITCH_ROM(saved);
}

UBYTE ReadSeqByteRaw(UBYTE chan, UBYTE step)
{
    UBYTE *p;

    p = SramSequence + ((unsigned int)step << 2);
    return ReadSongByteRaw(0, p + chan);
}

UBYTE ReadChainByte(UBYTE chain, UBYTE row)
{
    UBYTE *p;

    p = SramChainPhrases + ((unsigned int)chain << 4);
    return ReadSongByteRaw(1, p + row);
}

void SyncSeekSongPos(void)
{
    UBYTE chan, row, chain;

    SoundResetCommandState();
    for (chan = 0; chan < 4; chan++) {
        row = (wSyncMode == 1) ? wSyncRxPos : wSyncStartRow;
        chain = ReadSeqByteRaw(chan, row);
        while (row && (chain & 0x80)) {
            row--;
            chain = ReadSeqByteRaw(chan, row);
        }
        if (chain & 0x80) {
            wChannelActive[chan] = 0;
        } else {
            wChannelSongRow[chan] = row;
            wChannelChainRow[chan] = 0xFF;
            if ((wChannelPhrase[chan] = ReadChainByte(chain, 0)) == 0xFF) {
                wChannelActive[chan] = 0;
            } else {
                wChannelChain[chan] = chain;
                wChannelActive[chan] = 1;
            }
        }
    }
    wPlayMode = 4;
}

void SendByteStep(void)
{
    UBYTE saved, chan;

    wSyncTxByte = 0xFF;
    if (wSendByteNest++) {
        if (!wSendByteNest)
            HandleTooBusy();
        return;
    }
    saved = hCurrentBank;
    chan = hSoundChannel;
    do {
        _asm ei _endasm;
        if (wSyncRxPos <= 0xBF) {
            DIV_REG = 0;
            NR52_REG = 0x80;
            NR50_REG = 0x77;
            if (!wLiveMode) {
                SWITCH_ROM(2);
                SyncSeekSongPos();
                SoundEnableLcdInts();
                if (wCurrentScreen == 9) {
                    SWITCH_ROM(0x1B);
                    wPlayMinutes = 0;
                    wPlaySeconds = 0;
                    DrawSyncSubSetting();
                }
                SWITCH_ROM(saved);
            }
            wSyncRxPos = 0xFF;
        }
        if (wPlayMode) {
            if (wSyncMode == 7)
                SyncSendAnalogClock();
            SWITCH_ROM(2);
            SoundRunFrame();
            SWITCH_ROM(saved);
        }
        if (wSyncRun) {
            if ((SC_REG & 0x81) == 0x81)
                DebugTrap();
            SendSyncByte();
        }
        _asm di _endasm;
    } while (--wSendByteNest);
    _asm ei _endasm;
    hSoundChannel = chan;
}

void ProcessSyncByte(void)
{
    UBYTE saved;

    if (wSyncMode == 1 && !wSyncRun && wLiveStartColumn == 0xFF) {
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
            ReceiveLinkClipboard();
            SWITCH_ROM(saved);
            return;
        }
        SendSyncHandshake();
    }
    SendByteStep();
}

void SyncReceiveAnalogClock(void)
{
    UBYTE sb, steps;

    sb = SB_REG;
    steps = 0;
    if (!wSyncInGate && sb) {
        steps = wSyncInPulses + 1;
        if (wPlayMode) {
            wAnaInStepLen = ((UWORD)wAnaInPulseCount << 8) / wSyncMode2;
            wSyncInPulses = wSyncMode2 - 1;
            wAnaInStepAccum = 0;
            if (!wAnaInPrevPlayMode)
                steps++;
        }
        wAnaInPulseCount = 0;
        wAnaInPrevPlayMode = wPlayMode;
    } else {
        if (wSyncInPulses) {
            wAnaInStepAccum += 0x100;
            if (wAnaInStepAccum >= wAnaInStepLen) {
                wAnaInStepAccum -= wAnaInStepLen;
                steps = 1;
                wSyncInPulses--;
            }
        }
        wAnaInPulseCount++;
    }
    wSyncInGate = sb;
    SC_REG = 0x81;
    while (steps--)
        SendByteStep();
}
