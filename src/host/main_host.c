/*
 * waveod-sim: runs the application on the PC against a firmware image and a control script.
 *
 *   waveod-sim --fw QME-200_FW_USB_V1.2.2.bin [--out DIR] [--hold B] [--crash C,P,S,T,N[,A]] COMMAND...
 *
 * --hold B: button B held at power-up (EXIT = safe mode). --crash: the previous run ended in a crash of cause C
 * (enum crash_cause) in preset P, slot S (255 = none), T s after its start-up, the Nth in a row, A = 1 in audio.
 *
 * Commands:
 *   press:B      press and release button B (MENU EXIT EFX SAVE DRUM GLOBAL CTRL EXP A B C D UP DOWN FS1 FS2);
 *                several at once with +, e.g. press:A+B
 *   hold:B       long press
 *   turn:E:N     turn encoder E (MENU K1 K2 K3) by N detents
 *   pot:P:V      set MASTER / EXP1 / EXP2 to V (0..4095)
 *   tap:X,Y      touch and release at display pixel X,Y
 *   long:X,Y     touch for a second
 *   drag:X0,Y0:X1,Y1   touch at X0,Y0, slide to X1,Y1 in 200 ms, release
 *   down:X,Y / move:X,Y / up   one touch in steps (move slides there in 100 ms), e.g. to take shots mid-drag
 *   tone:HZ      a test tone on the guitar input (tone:0 = silence)
 *   cc:N:V       MIDI control change N at value V on the TRS input (channel 1)
 *   midiout      print what the TRS output sent since the last time (hex)
 *   loop:OUT:N   a cable from OUT (L, R, SEND; off) to the input (SEND: the FX return), N samples beyond the
 *                128 of the buffers (the latency test)
 *   wait:MS      let time pass
 *   shot:NAME    save the screen as DIR/NAME.png
 *   leds         print the footswitch LEDs
 */
#include "app/app.h"
#include "host/host.h"
#include "platform/controls.h"
#include "platform/safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP_MS 20

static const struct { const char *name; uint32_t mask; } buttons[] = {
    { "MENU", BTN_MENU }, { "EXIT", BTN_EXIT }, { "EFX", BTN_EFX }, { "SAVE", BTN_SAVE },
    { "DRUM", BTN_DRUM }, { "GLOBAL", BTN_GLOBAL }, { "CTRL", BTN_CTRL }, { "EXP", BTN_EXP },
    { "A", BTN_FS_A }, { "B", BTN_FS_B }, { "C", BTN_FS_C }, { "D", BTN_FS_D },
    { "UP", BTN_BANK_UP }, { "DOWN", BTN_BANK_DOWN }, { "FS1", BTN_EXT1 }, { "FS2", BTN_EXT2 },
};
static const char *const encoders[ENC_COUNT] = { "MENU", "K1", "K2", "K3" };
static const char *const pots[POT_COUNT] = { "MASTER", "EXP1", "EXP2" };

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        host_time_advance(STEP_MS);
        host_audio_run(STEP_MS * 44100 / 64 / 1000);
        app_step();
    }
}

static uint32_t button(const char *name)
{
    char one[16];
    uint32_t mask = 0;
    while (*name) {
        unsigned n = 0;
        while (name[n] && name[n] != '+' && n + 1 < sizeof one) {
            one[n] = name[n];
            n++;
        }
        one[n] = 0;
        name += n + (name[n] == '+');
        unsigned i = 0;
        while (i < sizeof buttons / sizeof buttons[0] && strcmp(buttons[i].name, one) != 0)
            i++;
        if (i == sizeof buttons / sizeof buttons[0]) {
            fprintf(stderr, "unknown button %s\n", one);
            exit(2);
        }
        mask |= buttons[i].mask;
    }
    return mask;
}

static int lookup(const char *const *names, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (strcmp(names[i], name) == 0)
            return i;
    fprintf(stderr, "unknown control %s\n", name);
    exit(2);
}

static int touch_x, touch_y;            /* finger of down / move / up */

static void command(const char *cmd, const char *out_dir)
{
    char a[32] = "", b[64] = "", c[32] = "";
    sscanf(cmd, "%31[^:]:%63[^:]:%31s", a, b, c);
    if (strcmp(a, "press") == 0 || strcmp(a, "hold") == 0) {
        uint32_t m = button(b);
        host_buttons_set(m, 1);
        run(strcmp(a, "hold") == 0 ? 800 : 60);
        host_buttons_set(m, 0);
        run(60);
    } else if (strcmp(a, "turn") == 0) {
        host_encoder_turn(lookup(encoders, ENC_COUNT, b), atoi(c));
        run(120);
    } else if (strcmp(a, "pot") == 0) {
        host_pot_set(lookup(pots, POT_COUNT, b), (uint16_t)atoi(c));
        run(100);
    } else if (strcmp(a, "tap") == 0 || strcmp(a, "long") == 0) {
        int x = 0, y = 0;
        sscanf(b, "%d,%d", &x, &y);
        host_touch_set(1, x, y);
        run(strcmp(a, "long") == 0 ? 1000 : 80);
        host_touch_set(0, x, y);
        run(100);
    } else if (strcmp(a, "drag") == 0) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        sscanf(b, "%d,%d", &x0, &y0);
        sscanf(c, "%d,%d", &x1, &y1);
        host_touch_set(1, x0, y0);
        run(STEP_MS * 2);
        for (int i = 1; i <= 10; i++) {
            host_touch_set(1, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10);
            run(STEP_MS);
        }
        host_touch_set(0, x1, y1);
        run(100);
    } else if (strcmp(a, "down") == 0) {
        sscanf(b, "%d,%d", &touch_x, &touch_y);
        host_touch_set(1, touch_x, touch_y);
        run(STEP_MS * 2);
    } else if (strcmp(a, "move") == 0) {
        int x = 0, y = 0;
        sscanf(b, "%d,%d", &x, &y);
        for (int i = 1; i <= 5; i++) {
            host_touch_set(1, touch_x + (x - touch_x) * i / 5, touch_y + (y - touch_y) * i / 5);
            run(STEP_MS);
        }
        touch_x = x;
        touch_y = y;
    } else if (strcmp(a, "up") == 0) {
        host_touch_set(0, touch_x, touch_y);
        run(100);
    } else if (strcmp(a, "tone") == 0) {
        host_audio_tone(atof(b), 0.05);
        run(200);
    } else if (strcmp(a, "loop") == 0) {
        host_audio_loop(strcmp(b, "L") == 0 ? 0 : strcmp(b, "R") == 0 ? 2 : strcmp(b, "SEND") == 0 ? 1 : -1, atoi(c));
        run(STEP_MS);
    } else if (strcmp(a, "cc") == 0) {
        uint8_t msg[3] = { 0xB0, (uint8_t)(atoi(b) & 0x7f), (uint8_t)(atoi(c) & 0x7f) };
        host_midi_in(msg, 3);
        run(60);
    } else if (strcmp(a, "midiout") == 0) {
        uint8_t buf[256];
        int n = host_midi_out(buf, sizeof buf);
        printf("midi out:");
        for (int i = 0; i < n; i++)
            printf(" %02X", buf[i]);
        printf("\n");
    } else if (strcmp(a, "wait") == 0) {
        run((uint32_t)atoi(b));
    } else if (strcmp(a, "shot") == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.png", out_dir, b);
        run(STEP_MS);
        if (host_display_save_png(path) == 0)
            printf("%s\n", path);
        else
            fprintf(stderr, "cannot write %s\n", path);
    } else if (strcmp(a, "leds") == 0) {
        char text[160];
        run(STEP_MS);
        host_leds_text(text, sizeof text);
        printf("leds: %s\n", text);
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    const char *fw = 0, *out = ".", *hold = 0;
    int first = argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fw") == 0 && i + 1 < argc)
            fw = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out = argv[++i];
        else if (strcmp(argv[i], "--hold") == 0 && i + 1 < argc)
            hold = argv[++i];
        else if (strcmp(argv[i], "--crash") == 0 && i + 1 < argc) {
            unsigned v[6] = { 0, 0, SAFETY_NONE, 0, 1, 0 };
            sscanf(argv[++i], "%u,%u,%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            struct crash_info c = { .cause = (uint8_t)v[0], .preset = (uint8_t)v[1], .slot = (uint8_t)v[2],
                                    .uptime_s = v[3], .count = (uint8_t)v[4], .in_audio = (uint8_t)v[5],
                                    .pc = 0x0005a1c2u, .detail = 0x00010000u };
            host_crash_set(&c);
        }
        else {
            first = i;
            break;
        }
    }
    if (!fw || host_flash_load_firmware(fw) != 0) {
        fprintf(stderr, "usage: waveod-sim --fw firmware.bin [--out DIR] command...\n");
        return 1;
    }
    if (hold)
        host_buttons_set(button(hold), 1);
    app_init();
    run(200);
    if (hold)
        host_buttons_set(button(hold), 0);
    for (int i = first; i < argc; i++)
        command(argv[i], out);
    return 0;
}
