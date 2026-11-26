/*
 * Include last in a screen's .c file: its code and constants go to SDRAM (cached) instead of ITCM, which is
 * kept for the audio path and the rest of the application (480 KB shared with the effect module pool). The
 * screens run in the main loop, like LVGL, which runs from SDRAM too. A pragma and not a linker rule, because
 * the firmware is linked with LTO (one object, no file names left).
 */
#if !defined(WAVEOD_HOST) && defined(__clang__)
#pragma clang section text = ".sdram_text" rodata = ".sdram_const"
#endif
