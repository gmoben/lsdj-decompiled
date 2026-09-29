/*
 * Sync and transport helpers -- LSDj 9.4.0, ROM bank $00:$1CFF-$24A7.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The sync mode setters, the CPU-overload message and the unchecked
 * sequence/chain readers the song-position sync uses (ReadSeqByte's twins
 * without the asserts).
 */

#include "lsdj.h"

extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wSyncSendGate;                 /* $CB6E */
extern char TxtTooBusy[];                   /* $01:$6FAE, "TOO BUSY!" */

void SndInitPlayState(void);                /* bank 2 */
void SetInterruptEnable(UBYTE ie);          /* asm */
void SendSyncHandshake(void);

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
