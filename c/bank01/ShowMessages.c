/*
 * ShowOutOfMemory / ShowTapAddChain -- LSDj 9.4.0, ROM bank $01:$75BD / $75F8
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank01/ShowMessages.c --target ShowOutOfMemory
 *     tools/match/match_function.py --c c/bank01/ShowMessages.c --target ShowTapAddChain
 *
 * One-line wrappers that pop up a fixed message via bank 0's ShowMessage
 * (which copies the string into wStrBuffer; DrawMessageBar then renders it).
 */

extern char TxtOutOfMemory[];   /* $01:$75C7 "OUT OF MEMORY. CLEAN SONG DATA IN PROJECT SCREEN" */
extern char TxtTapAddChain[];   /* $01:$7602 "TAP A TO ADD CHAIN!" */

void ShowMessage(char *text);

void ShowOutOfMemory(void)
{
    ShowMessage(TxtOutOfMemory);
}

void ShowTapAddChain(void)
{
    ShowMessage(TxtTapAddChain);
}
