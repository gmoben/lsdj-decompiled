; Macros the carved banks use (src/main.asm includes this first).

; RGB r, g, b[, r, g, b ...]: CGB colours, each channel 0-31, red in the low
; bits (the colour sets, ColorSetPalettes).
MACRO RGB
	REPT _NARG / 3
		dw (\1) | (\2) << 5 | (\3) << 10
		SHIFT 3
	ENDR
ENDM
