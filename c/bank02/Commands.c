/*
 * The commands written in C -- LSDj 9.4.0, ROM bank $02.
 *
 * The phrase and table commands' C half: E's envelope restarts, K, O, P, R,
 * T, Z and the table's maybe-hop, the noise channel's pitch and chord
 * steps, a delayed command's slot.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChannel;    /* $FF8B */
extern volatile sfr UBYTE hCommandValue;    /* $FF8F */
extern UBYTE hCommandValueMemory;           /* hCommandValue read as a plain global (ld hl; or a,(hl)) */
extern volatile sfr BYTE hCommandValueS;    /* hCommandValue as a signed byte (P and F's steps) */
extern UBYTE wCommandChannel;               /* $C527: the FX slot (channel + 4 x column) */
extern UBYTE wCommandSlotCommandTimer[];    /* $C230, [12] */
extern UBYTE wChannelCommandSlots[];        /* $C2F0, [12] */
extern UBYTE wWavLevel;                     /* $C8F3 */
extern BYTE wWavLevelS;                     /* wWavLevel as a signed byte */
extern UBYTE wSpeechWord;                   /* $C4CE */
extern UWORD wChannelPitchRate[];           /* $C2D0, [4] */
extern UBYTE wChannelTable[];               /* $C204, [4] */
extern UBYTE wChannelDisplayNote[];         /* $C41F, [4] */
extern UBYTE wChannelChord[];               /* $C218, [4] */
extern UBYTE wChannelNoteCountdown[];       /* $C160, [8]: the phrase's 4, the table's 4 */
extern UBYTE wChannelGrooveRow[];           /* $C158, [8] */
extern UBYTE wChannelTableRow1[];           /* $C208, [8] */
extern UBYTE wChannelDrumPitch[];           /* $C330, [4] */
extern UWORD wChannelPitchCurrent[];        /* $C337, [4] */
extern UBYTE wChannelCommandStep[];         /* $C34E, [4] */
extern UBYTE wChannelTableActive[];         /* $C2E8, [4] */
extern UBYTE wChannelTableLatch[];          /* $C220, [4] */
extern UBYTE wChannelTableFresh[];          /* $C2EC, [4] */
extern UBYTE wChannelSweep[];               /* $C2E4, [4]: [3] is the noise note */
extern UBYTE wNoiseNoteLatch;               /* $C4FD */
extern UBYTE wKitVoicesActive;              /* $C8F6 */
extern UBYTE wSoundUpdateFlags;             /* $C8F5 */
extern UBYTE wEnvelopeNode1Pu1, wEnvelopeNode1Pu2, wEnvelopeNode1Noi; /* $CBBC, $CBC3, $CBCA */
extern UBYTE wNoiPitchFraction;             /* $C8DB */
extern UBYTE wNoiVibratoPreviousOffset;     /* $C8DA */
extern UBYTE wChannelVibratoSpeed[];        /* $C314, [4] words, read here by byte */
extern UWORD wChannelFrequency[];           /* $C0F4, [4] */
extern UBYTE wKit1OffsetHigh, wKit1OffsetLow; /* $C4EA, $C4EB */
extern UBYTE wKit2OffsetHigh, wKit2OffsetLow; /* $C4EC, $C4ED */
extern UBYTE wTableCommand1Value;           /* $C36F */
extern UBYTE wTableCommand2Value;           /* $C370 */
extern UBYTE wChannelVibratoShape[];        /* $C35F, [4] */
extern UBYTE wNoiPSlideStart;               /* $C8C5 */
extern UBYTE wChannelCommandRate[];         /* $C8C6, [4] */
extern UBYTE wChannelRetrigTimer[];         /* $C148, [4] */
extern UBYTE wChannelRetrigRate[];          /* $C14C, [4] */
extern UBYTE wCommandSlotRetrigRate[];      /* $C8CE, [12] */
extern UBYTE wChannelEnvelopeVolumeOffset[]; /* $C224, [12]: R's volume digit, by FX slot */
extern UBYTE wCommandSlotEnvelopeTimer[];   /* $C23C, [12] */
extern UBYTE wChannelCommand[];             /* $C365, [6]: 4 phrase channels, the table's CMD1 and CMD2 */
extern UBYTE wChannelCommandValue[];        /* $C36B, [6] */
extern UBYTE wLastCommand[];                /* $C371, [68] */
extern UBYTE wLastCommandValue[];           /* $C3B5, [68] */
extern UBYTE wWavSynthFrame;                /* $C694 */
extern UBYTE wChannelFrequencyHigh[];       /* $C2E0, [4] */
extern UBYTE wChannelEnvelope[];            /* $C2D8, [4] */
extern UBYTE wChannelChordPhase[];          /* $C21C, [4] */
extern UBYTE wNoiChordPreviousOffset;       /* $C8DC */
extern UBYTE wEnvelopeStagePu1, wEnvelopeSpeedPu1, wEnvelopeCounterPu1, wEnvelopeTargetPu1; /* $CBBF.. */
extern UBYTE wEnvelopeStagePu2, wEnvelopeSpeedPu2, wEnvelopeCounterPu2, wEnvelopeTargetPu2; /* $CBC6.. */
extern UBYTE wEnvelopeStageNoi, wEnvelopeSpeedNoi, wEnvelopeCounterNoi, wEnvelopeTargetNoi; /* $CBCD.. */
void SoundWritePanning(UBYTE v);            /* bank 0 */
UBYTE randw(void);                          /* bank 0 (this file reads the low byte) */
UBYTE MulU8(UBYTE a, UBYTE b);              /* bank 0 */
void SetSyncSendRate(void);                 /* bank 7 */
UBYTE SoundRandomUpTo(UBYTE n);
void SoundTableHop(UBYTE v);                /* asm */
void SoundApplyPitchBend(void);             /* asm */
void SoundKillNote(void);
void SoundResetChannelCommand(void);        /* asm */
void SoundRetrigNote(void);
void SoundTriggerWav(void);                 /* asm */
void SoundClearChannelCommandSlots(void);   /* asm */
void SoundClearVoiceHram(void);             /* asm */
void SoundLoadWaveRam(void);                /* bank 0, asm */
void SoundTriggerPu1(void);                 /* asm */
void SoundTriggerPu2(void);                 /* asm */
void SoundTriggerNoi(void);                 /* asm */
void SoundSetupLfo(void);                   /* asm */
UBYTE SoundEnvelopeReleaseSpeed(void);
UBYTE SoundEnvelopeReleaseTarget(void);
void SoundClampNoteRange(UBYTE n);          /* asm */
void SoundCommandParsePitchValue(UBYTE *envelope); /* asm */
void SoundReadTableEnvelope(void);          /* asm */
void SoundReadTableTranspose(void);         /* asm */
void SoundTriggerNote(void);                /* asm */
void SoundSetKitParameters(void);           /* asm */
UBYTE SoundStepPitchVibratoNoi(void);       /* bank 0, asm */
void SoundWavLevelStep(UBYTE v);            /* defined below */

/* $6479: a random number 0..n (the Z command's digits) */

UBYTE SoundRandomUpTo(UBYTE n)
{
    if (!n)
        return 0;
    return randw() % (n + 1);
}

/* $649D: R, retrig (R8x: resync). The first digit changes the volume on each
   retrig; the second sets the rate in command-rate steps (1 fastest, F
   slowest, 0 once: stored as $0F) */

void Command_R(void)
{
    UBYTE volume;
    UBYTE n;
    UBYTE channel;

    volume = hCommandValue & 0xF0;
    if (volume == 0x80) {
        n = hCommandValue & 0x0F;
        n = MulU8(n, wChannelCommandRate[hSoundChannel] + 1);
        channel = hSoundChannel;
        wChannelRetrigTimer[channel] = ++n;
        wChannelRetrigRate[channel] = n & 0x0F;
    } else {
        n = (hCommandValue - 1) & 0x0F;
        if (n != 0x0F)
            n = MulU8(n, wChannelCommandRate[hSoundChannel] + 1);
        wCommandSlotRetrigRate[wCommandChannel] = n;
        wChannelCommandSlots[wCommandChannel] |= 2;
        wChannelEnvelopeVolumeOffset[wCommandChannel] = volume;
        if (wCommandChannel < 4)
            wCommandSlotEnvelopeTimer[wCommandChannel] = 0;
        else
            SoundRetrigNote();
    }
}

/* $6577: kill this channel's note: its FX and FX slots, pitch slide, table,
   chord and vibrato off */

void SoundKillNote(void)
{
    UBYTE channel;

    SoundResetChannelCommand();
    wChannelCommandSlots[hSoundChannel] = 0;
    wChannelCommandSlots[hSoundChannel + 4] = 0;
    wChannelCommandSlots[hSoundChannel + 8] = 0;
    wChannelPitchRate[hSoundChannel] = 0;
    channel = hSoundChannel;
    wChannelTable[channel] = 0xFF;
    wChannelDisplayNote[channel] = 0;
    wChannelChord[channel] = 0;
    wChannelVibratoShape[channel] = 0;
}

/* $65F3: K, kill the note: now (K00) or after the value's ticks, one fewer
   from a table column */

void Command_K(void)
{
    if (hCommandValueMemory) {
        wCommandSlotCommandTimer[wCommandChannel] = hCommandValue - (wCommandChannel > 3);
        wChannelCommandSlots[wCommandChannel] |= 1;
    } else
        SoundKillNote();
}

/* $6646: P, the pitch bend; on the noise channel P applies S every tick */

void Command_P(void)
{
    if (hSoundChannel == CHANNEL_NOI) {
        wChannelVibratoShape[3] = hCommandValue;
        wNoiPSlideStart = 1;
    } else
        SoundApplyPitchBend();
}

/* $665F: O, the output (panning) */

void Command_O(void)
{
    SoundWritePanning(hCommandValue);
}

/* $6669: T, the tempo */

void Command_T(void)
{
    wTempo = hCommandValue;
    wTempoMode = TEMPO_BPM;
    FarCall(7, FAR_FN(SetSyncSendRate));
}

/* $66E7: R fires on FX slot wCommandChannel and the note retriggers: a slide
   pending on a pulse or the wave channel restarts and the envelope is parsed
   again (the wave's level steps by R's volume digit instead); a phrase slot
   restarts its channel's latched table too. Then the slot reloads its timer
   from its rate, or ends (rate $0F: once) */

void SoundRetrigNote(void)
{
    UBYTE rate;

    hSoundChannel = wCommandChannel & 3;
    if (!wSpeechWord || hSoundChannel != CHANNEL_WAV) {
        switch (hSoundChannel) {
        case CHANNEL_PU1:
            if (wChannelDrumPitch[0]) {
                wChannelPitchCurrent[0] = 0x8000;
                wChannelCommandStep[0] = 1;
            }
            SoundCommandParsePitchValue(&wEnvelopeNode1Pu1);
            break;
        case CHANNEL_PU2:
            if (wChannelDrumPitch[1]) {
                wChannelPitchCurrent[1] = 0x8000;
                wChannelCommandStep[1] = 1;
            }
            SoundCommandParsePitchValue(&wEnvelopeNode1Pu2);
            break;
        case CHANNEL_WAV:
            if (wChannelDrumPitch[2]) {
                wChannelPitchCurrent[2] = 0x8000;
                wChannelCommandStep[2] = 1;
            }
            SoundWavLevelStep(wChannelEnvelopeVolumeOffset[wCommandChannel]);
            break;
        case CHANNEL_NOI:
            SoundCommandParsePitchValue(&wEnvelopeNode1Noi);
            break;
        }
        if (wCommandChannel < 4 && wChannelTableActive[hSoundChannel]) {
            wChannelTable[hSoundChannel] = wChannelTableLatch[hSoundChannel];
            wChannelTableRow1[hSoundChannel + 4] = 0xFF;
            wChannelTableRow1[hSoundChannel] = 0xFF;
            wChannelGrooveRow[hSoundChannel + 4] = 0xFF;
            wChannelNoteCountdown[hSoundChannel + 4] = 0;
            SoundReadTableEnvelope();
            SoundReadTableTranspose();
            wChannelTableFresh[hSoundChannel] = 1;
        }
    }
    if (hSoundChannel == CHANNEL_NOI)
        wChannelSweep[hSoundChannel] = wNoiseNoteLatch;
    SoundTriggerNote();
    if (hSoundChannel == CHANNEL_WAV) {
        wKitVoicesActive = wSoundUpdateFlags;
        SoundSetKitParameters();
    }
    rate = wCommandSlotRetrigRate[wCommandChannel];
    if (rate == 0x0F)
        wChannelCommandSlots[wCommandChannel] = 0;
    else
        wCommandSlotEnvelopeTimer[wCommandChannel] = rate;
}

/* $6270: P with the instrument's PITCH on STEP (SoundApplyPitchBend jumps
   here): the pitch changes at once -- a kit's playback rate by 3 a step,
   any other instrument's pitch by 8 a step, its slide restarted */

void Command_P_Step(void)
{
    if (wSoundUpdateFlags && hSoundChannel == CHANNEL_WAV)
        wChannelFrequency[hSoundChannel] += (WORD)hCommandValueS * 3;
    else {
        wChannelPitchCurrent[hSoundChannel] += (WORD)hCommandValueS << 3;
        wChannelCommandStep[hSoundChannel] = 1;
    }
}

/* $632B: F on the wave channel: a synth steps its wave frame by the value
   and reloads the wave RAM; a kit moves both voices' sample positions by
   the signed value x16 (beyond +-16 the steps double), stopping at the
   sample's start, and retriggers at the instrument's level */

void Command_F_Wav(void)
{
    WORD step;
    UWORD off;

    if (!wSoundUpdateFlags) {
        if (hCommandValueMemory) {
            wWavSynthFrame = hCommandValue + wWavSynthFrame;
            SoundClearVoiceHram();
            _asm di _endasm;
            SoundLoadWaveRam();
            _asm ei _endasm;
        }
    } else {
        step = hCommandValueS;
        off = wKit1OffsetHigh;
        if (step >= 16) {
            step <<= 1;
            step -= 16;
        } else if (step < -16) {
            step <<= 1;
            step += 16;
        }
        step <<= 4;
        off <<= 8;
        off += wKit1OffsetLow;
        off += step;
        if (off & 0x8000)
            off = 0;
        wKit1OffsetLow = off & 0xFF;
        wKit1OffsetHigh = off >> 8;
        off = wKit2OffsetHigh;
        off <<= 8;
        off += wKit2OffsetLow;
        off += step;
        if (off & 0x8000)
            off = 0;
        wKit2OffsetLow = off & 0xFF;
        wKit2OffsetHigh = off >> 8;
        step = wWavLevelS;
        wWavLevel = wChannelEnvelope[2];
        SoundTriggerWav();
        wWavLevel = step;
        SoundClearChannelCommandSlots();
    }
}

/* $68A0: the noise channel's pitch each tick: P slides it by its signed
   speed in quarter semitones (the remainder carried in wNoiPitchFraction), then
   the vibrato's offset applies, both as note steps through SoundClampNoteRange */

void SoundNoiPitchStep(void)
{
    UBYTE v, neg, offset;

    v = wChannelVibratoShape[3];
    if (v) {
        if (wNoiPSlideStart) {
            wNoiPSlideStart = 0;
            wNoiPitchFraction = 0;
        } else {
            neg = v & 0x80;
            if (neg)
                v = -v;
            v = v + wNoiPitchFraction;
            wNoiPitchFraction = v & (UBYTE)3;
            v >>= 2;
            if (neg)
                v = -v;
            SoundClampNoteRange(v);
        }
    }
    if (wChannelVibratoSpeed[7]) {
        offset = SoundStepPitchVibratoNoi();
        SoundClampNoteRange(offset - wNoiVibratoPreviousOffset);
        wNoiVibratoPreviousOffset = offset;
    }
}

/* $6904: step the noise channel's chord: the note, the first digit's
   offset, the second's (skipped when 0), the note again */

void SoundNoiChordStep(void)
{
    UBYTE chord;
    UBYTE *q;

    chord = wChannelChord[3];
    if (chord) {
        q = wChannelChordPhase;
        switch (q[3]) {
        case 0:
            q[3] = 1;
            wNoiChordPreviousOffset = 0;
            break;
        case 1:
            q[3] = 2;
            chord >>= 4;
            SoundClampNoteRange(chord);
            wNoiChordPreviousOffset = chord;
            break;
        case 2:
            if (chord & 0x0F) {
                q[3] = 3;
                chord &= 0x0F;
                SoundClampNoteRange(chord - wNoiChordPreviousOffset);
                wNoiChordPreviousOffset = chord;
                break;
            }
        case 3:
            q[3] = 1;
            SoundClampNoteRange(-wNoiChordPreviousOffset);
            wNoiChordPreviousOffset = 0;
            break;
        }
    }
}

const UBYTE EnvelopeReleaseSpeeds[8] = {0x00, 0x06, 0x0B, 0x11, 0x16, 0x1C, 0x22, 0x27};

/* $69C3: E's release digit (the value's low three bits) -> the fade speed */

UBYTE SoundEnvelopeReleaseSpeed(void)
{
    return EnvelopeReleaseSpeeds[hCommandValue & 7];
}

/* $6685: a delayed K's slot ran out: kill that channel's note */

void SoundRunCommandSlot(void)
{
    hSoundChannel = wCommandChannel & 3;
    SoundKillNote();
}

/* $6694: step the WAV output level (NR32 bits 5-6: $20 100%, $40 50%,
   $60 25%, 0 off) by the signed high digit of v: negative fades towards
   off, positive towards 100% */

void SoundWavLevelStep(UBYTE v)
{
    if (!v)
        return;
    wWavLevel &= 0x60;
    if (v & 0x80) {
        while (v && wWavLevel) {
            wWavLevel += 0x20;
            wWavLevel &= 0x60;
            v += 0x10;
        }
    } else {
        while (v && wWavLevel != 0x20) {
            wWavLevel -= 0x20;
            wWavLevel &= 0x60;
            v -= 0x10;
        }
    }
}

/* $69DF: E's release direction (the second digit's bit 3: 9-F fade up): the
   volume the envelope fades toward */

UBYTE SoundEnvelopeReleaseTarget(void)
{
    return (hCommandValue & 8) ? 0x0F : 0;
}

/* $69F2: E on PU1: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Pu1(void)
{
    UBYTE v;

    if (wChannelFrequencyHigh[0] & 0x40)
        SoundTriggerPu1();
    wEnvelopeStagePu1 = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedPu1 = v;
    if (v) {
        wEnvelopeCounterPu1 = wEnvelopeSpeedPu1;
        wEnvelopeTargetPu1 = SoundEnvelopeReleaseTarget();
        wEnvelopeStagePu1 = 3;
    }
}

/* $6A28: E on PU2: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Pu2(void)
{
    UBYTE v;

    if (wChannelFrequencyHigh[1] & 0x40)
        SoundTriggerPu2();
    wEnvelopeStagePu2 = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedPu2 = v;
    if (v) {
        wEnvelopeCounterPu2 = wEnvelopeSpeedPu2;
        wEnvelopeTargetPu2 = SoundEnvelopeReleaseTarget();
        wEnvelopeStagePu2 = 3;
    }
}

/* $6A5E: E on NOI: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Noi(void)
{
    UBYTE v;

    if (wChannelFrequencyHigh[3] & 0x40)
        SoundTriggerNoi();
    wEnvelopeStageNoi = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedNoi = v;
    if (v) {
        wEnvelopeCounterNoi = wEnvelopeSpeedNoi;
        wEnvelopeTargetNoi = SoundEnvelopeReleaseTarget();
        wEnvelopeStageNoi = 3;
    }
}

/* $7362: B in a table, MayBe hop: with the probability in the high digit of
   the command value (of the right column if `right`), hop to the row in the
   low digit */

UBYTE SoundTableMaybeHop(UBYTE right)
{
    UBYTE v;

    v = right ? wTableCommand2Value : wTableCommand1Value;
    if ((v & 0xF0) > randw()) {
        v &= 0x0F;
        if (right)
            v |= 0x80;
        SoundTableHop(v);
        return 1;
    }
    return 0;
}

/* $73B0: Z for command slot `slot`: repeat the last command that was not Z
   or H (wLastCommand[last]) with its value plus 0..each Z digit; returns the
   command */

UBYTE SoundZRepeat(UBYTE slot, UBYTE last)
{
    wChannelCommandValue[slot] = wLastCommandValue[last] + (SoundRandomUpTo(wChannelCommandValue[slot] >> 4) << 4)
                        + SoundRandomUpTo(wChannelCommandValue[slot] & 0x0F);
    wChannelCommand[slot] = wLastCommand[last];
    return wChannelCommand[slot];
}

/* $743E: Z in a phrase: this channel's last command */

UBYTE SoundZPhrase(void)
{
    return SoundZRepeat(hSoundChannel, hSoundChannel);
}

/* $744D: Z in a table's CMD1 column: that table's last CMD1 */

UBYTE SoundZTableCommand1(void)
{
    return SoundZRepeat(4, wChannelTable[hSoundChannel] + 4);
}

/* $746F: Z in a table's CMD2 column */

UBYTE SoundZTableCommand2(void)
{
    return SoundZRepeat(5, wChannelTable[hSoundChannel] + 0x24);
}

/* $7CFB: E at a note-on: on PU1, PU2 and NOI stop the fade and take the
   first digit as the envelope's volume (e = 5: the release runs as the note
   plays); on WAV set the output level from the value (e = 0) */

UBYTE Command_E_NoteOn(void)
{
    UBYTE v;

    v = wChannelCommandValue[hSoundChannel];
    switch (hSoundChannel) {
    case CHANNEL_PU1:
        wEnvelopeStagePu1 = 0;
        wEnvelopeSpeedPu1 = 0;
        break;
    case CHANNEL_PU2:
        wEnvelopeStagePu2 = 0;
        wEnvelopeSpeedPu2 = 0;
        break;
    case CHANNEL_WAV:
        v <<= 4;
        wWavLevel = ~(v << 1) + 1;
        return 0;
    case CHANNEL_NOI:
        wEnvelopeStageNoi = 0;
        wEnvelopeSpeedNoi = 0;
        break;
    }
    wChannelEnvelope[hSoundChannel] = v >> 4;
    return 5;
}
