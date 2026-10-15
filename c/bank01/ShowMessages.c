/*
 * The message pop-ups -- LSDj 9.4.0, ROM bank $01 ($75BD..$763F)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank01/ShowMessages.c --target ShowOutOfMemory
 *     tools/match/match_function.py --c c/bank01/ShowMessages.c --target ShowTapAddChain
 *
 * One-line wrappers that pop up a fixed message via bank 0's ShowMessage
 * (which copies the string into wStringBuffer; DrawMessageBar then renders it).
 */

#include "lsdj.h"

extern char TxtOutOfMemory[];               /* $01:$75C7 "OUT OF MEMORY. CLEAN SONG DATA IN PROJECT SCREEN" */
extern char TxtTapAddChain[];               /* $01:$7602 "TAP A TO ADD CHAIN!" */

/* $75BD: push TxtOutOfMemory, call ShowMessage */
void ShowOutOfMemory(void)
{
    ShowMessage(TxtOutOfMemory);
}

/* $75F8: push TxtTapAddChain, call ShowMessage */
void ShowTapAddChain(void)
{
    ShowMessage(TxtTapAddChain);
}

/* $7616: push TxtTapAddPhrase, call ShowMessage */
void ShowTapAddPhrase(void)
{
    ShowMessage("TAP A TO ADD PHRASE!");
}

/* $7635: push TxtMax16Marks, call ShowMessage */
void ShowMax16Marks(void)
{
    ShowMessage("MAX 16 MARKS");
}
