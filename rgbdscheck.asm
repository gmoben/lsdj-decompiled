; Guard against building with an incompatible RGBDS toolchain.
; This project is developed and verified against rgbds v1.0.1.

IF !DEF(__RGBDS_MAJOR__)
	FAIL "lsdj-decompiled requires rgbds v1.0.0 or newer."
ENDC
IF __RGBDS_MAJOR__ < 1
	FAIL "lsdj-decompiled requires rgbds v1.0.0 or newer."
ENDC
