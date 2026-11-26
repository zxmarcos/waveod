/*
 * Start-up animation with the waveOD logo (tools/mkimages.py cuts it into three pieces): the wave is drawn
 * from left to right into the pedal, the name rises into place, the tagline lights up, then the home screen
 * fades in. About 2.5 s; any control or touch skips it. The audio runs from the start, under it.
 */
#pragma once

/* Build and show the animation (call once the screens exist). */
void boot_start(void);

/* 1 while the animation is on screen. */
int boot_running(void);

/* Call every UI tick; returns 1 once, when the animation is over and `next` must be shown. */
int boot_done(void);

/* Cut it short (a control was used). */
void boot_skip(void);
