/*
 * Include last (after the other includes) in a .c file of main-loop code: its code and constants go to SDRAM
 * (cached) instead of ITCM, which is kept for the audio path (480 KB shared with the effect module pool). For the
 * screens, the editor protocol, the preset formats: code the audio interrupt never runs. A pragma and not a linker
 * rule, because the firmware is linked with LTO (one object, no file names left).
 */
#if !defined(WAVEOD_HOST) && defined(__clang__)
#pragma clang section text = ".sdram_text" rodata = ".sdram_const"
#endif
