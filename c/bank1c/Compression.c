/*
 * Song compression -- LSDj 9.4.0, ROM bank $1C.
 *
 * The compressor and decompressor between the 32 KiB working song and
 * SRAM's file blocks.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wFileBlockIndex;                 /* $C958 */
extern UBYTE wCompressBank;                     /* $C979 */
extern UWORD wCompressSource;                      /* $C97A, an SRAM address */
extern UWORD wCompressDestination;                      /* $C97D, a working-song offset */
extern UBYTE wDecompressPosition;                    /* $C983 */
extern UBYTE wWorkSongPage;                 /* $C986, ReadWorkingSongByte's cached page */
extern UBYTE wDecompressBuffer[];                  /* $DA00, [256] */
extern UBYTE DefaultWaveFrameB1C[];         /* $1C:$4047 */
extern void (*wBlockSinkPointer)(void);         /* $C984, FlushBlockToSram or the verify sink */
extern UBYTE wCompressProgress;                 /* $C981 */
extern UBYTE wFileFirstCharacter;                /* $C980 */
UBYTE ReadCompressedByte(void);
void FilePutDecompressByte(UBYTE v);
void EmitDefaultWave(void);
void EmitDefaultInstrument(void);
void FlushBlockToSram(void);                /* $1C:$743C */
UBYTE FindFreeBlock(UBYTE *bank, UWORD *source);   /* $1C:$415A */
UBYTE CompressSong(void);                   /* $1C:$441F */
void LoadCompressedBlock(UBYTE v);
UBYTE FileClassifyToken(void);              /* asm */
UBYTE ReadWorkingSongByte(UWORD address);      /* asm */
void WriteCompressedByte(UBYTE v);          /* asm, v also in a */
void FileCompressRun(UBYTE v);              /* asm, v in a */

void FileWriteCompressedSram(void)
{
    if (!FileClassifyToken())
        DebugTrap();
    rRAMB = wCompressBank;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    memcpy((UBYTE *)wCompressSource, wDecompressBuffer, 0x100);
    rRAMG = 0;
    _asm
        ei
    _endasm;
    wCompressSource += 0x100;
    wDecompressPosition = 0;
}

UBYTE CompressIsDefaultWave(void)
{
    UBYTE byte;

    if (wCompressDestination >= (UWORD)0x7FF1)
        return 0;
    for (byte = 0; byte < 0x10; byte++) {
        if (ReadWorkingSongByte(wCompressDestination + byte) != DefaultWaveFrameB1C[byte])
            return 0;
    }
    return 1;
}

/*
 * SAVE's compressor: walks the working song with the current and next byte
 * and writes the block stream through WriteCompressedByte -- $C0 $C0 and
 * $E0 $E0 escape the two marker bytes, a byte repeated four times becomes an
 * RLE run (FileCompressRun), consecutive default wave frames an `$E0 $F0 n`
 * run. Near the end of a block's second page it links to the next free block
 * (`$E0 n`); 0 when none is left, 1 after the closing `$E0 $FF`.
 */
UBYTE CompressSong(void)
{
    UBYTE current;
    UBYTE next;
    UBYTE k;
    UBYTE bank;
    UWORD source;

    wCompressDestination = 0;
    wDecompressPosition = 0;
    wWorkSongPage = 0xFF;
    current = ReadSongByte(0, SRAM_PHRASE_NOTES);
    next = ReadSongByte(0, SRAM_PHRASE_NOTES + 1);
    do {
        if (current == COMPRESS_RLE) {
            WriteCompressedByte(COMPRESS_RLE);
            WriteCompressedByte(COMPRESS_RLE);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == 0x71 && next == 0x32 && CompressIsDefaultWave()) {
            k = 0;
            do {
                k = k + 1;
                wCompressDestination += 0x10;
            } while (CompressIsDefaultWave() && k != 0xFF);
            WriteCompressedByte(COMPRESS_COMMAND);
            WriteCompressedByte(COMPRESS_DEFAULT_WAVE);
            WriteCompressedByte(k);
            current = ReadWorkingSongByte(wCompressDestination);
            next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == COMPRESS_COMMAND) {
            WriteCompressedByte(COMPRESS_COMMAND);
            WriteCompressedByte(COMPRESS_COMMAND);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == next && wCompressDestination < 0x7FFD
                   && next == ReadWorkingSongByte(wCompressDestination + 2)
                   && ReadWorkingSongByte(wCompressDestination + 2) == ReadWorkingSongByte(wCompressDestination + 3)) {
            FileCompressRun(current);
            if (wCompressDestination < 0x8000) {
                current = ReadWorkingSongByte(wCompressDestination);
                if (wCompressDestination < 0x7FFF)
                    next = ReadWorkingSongByte(wCompressDestination + 1);
            }
        } else {
            WriteCompressedByte(current);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        }
        if (wDecompressPosition > 0xFB && (wCompressSource & 0x100)) {
            WriteCompressedByte(COMPRESS_COMMAND);
            if (!FindFreeBlock(&bank, &source))
                return 0;
            WriteCompressedByte(wFileBlockIndex);
            if (wDecompressPosition)
                FileWriteCompressedSram();
            wCompressBank = bank;
            wCompressSource = source;
        }
    } while (!(wCompressDestination & 0x8000));
    WriteCompressedByte(COMPRESS_COMMAND);
    WriteCompressedByte(COMPRESS_END);
    if (wDecompressPosition)
        FileWriteCompressedSram();
    return 1;
}

UBYTE DecompressBlock(void)
{
    UBYTE b;

    switch (b = ReadCompressedByte()) {
    case COMPRESS_COMMAND:
        FilePutDecompressByte(COMPRESS_COMMAND);
        break;
    case COMPRESS_DEFAULT_WAVE:
        b = ReadCompressedByte();
        while (b--)
            EmitDefaultWave();
        break;
    case COMPRESS_DEFAULT_INSTRUMENT:
        b = ReadCompressedByte();
        while (b--)
            EmitDefaultInstrument();
        break;
    case COMPRESS_END:
        return 1;
    default:
        if (wBlockSinkPointer == FlushBlockToSram) {
            wVramCursor = BG_MAP(12, 7);
            wCompressProgress++;
            DrawByte(wCompressProgress);
            DrawStringIndexed('/');
            DrawByte(wFileFirstCharacter);
        }
        LoadCompressedBlock(b);
    }
    return 0;
}
