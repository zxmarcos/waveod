/*
 * Groups 1, 3 and 6 of the editor protocol: the global settings (FUN_0002D120 block, the single parameters of
 * tables 0x42CEC..0x42D84), the drum machine's state, the global EQ.
 *
 * The settings are record 0 (fw/globals_format.h). A change is made on the record's bytes the way the
 * original's setter does it (clamps included), then the record is decoded and applied again, so the fields
 * waveod does not use survive too; record 0 is written after a pause (the original's +0xBFC flag).
 * Answers are notifications {sub, value} (FUN_0003568C).
 */
#include "editor/server.h"
#include "engine/drums.h"
#include "fw/bytes.h"
#include "model/drum_names.h"
#include "model/drum_user.h"
#include "model/globals.h"
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define LANGUAGES       5                   /* FUN_0002B024 */

static uint8_t rec[GLOBALS_RECORD_SIZE] SDRAM_BSS;

static void load(void)
{
    globals_record(rec);
}

static void store(void)
{
    globals_decode(rec, srv.globals);
    globals_apply(srv.globals);
    globals_touch();
}

static void answer(uint32_t sub, uint32_t value)
{
    notify_words(CMD_GLOBAL_SUB, 2, sub, value);
}

static int tempo_valid(void)
{
    uint16_t bpm = rd16(rec + 0x234);
    return bpm >= 40 && bpm <= 300;
}

/* FUN_0002D120: the settings block */
static void settings_block(void)
{
    uint8_t p[0x34];
    load();
    memset(p, 0, sizeof p);
    p[2] = srv.run_mode;
    float m = srv.master * 100.0f;
    p[3] = (uint8_t)(m < 0 ? 0 : m > 100 ? 100 : m);
    p[5] = rec[0x11];
    p[6] = rec[0x230];
    p[7] = rec[0x12];
    p[9] = rec[0x14];
    p[10] = rec[0x15];
    p[11] = rec[0x16];
    memcpy(p + 12, rec + 0x18, 2);
    memcpy(p + 14, rec + 0x1a, 0x2e - 0x1a);   /* +0x1A..+0x2D */
    p[34] = rec[0x2d];
    p[35] = rec[0x2f];
    memcpy(p + 36, rec + 0x70, 8);
    memcpy(p + 44, rec + 0x78, 3);
    p[47] = rec[0x232] > 100 ? 50 : rec[0x232];
    memcpy(p + 48, rec + 0x234, 2);
    p[50] = tempo_valid() ? rec[0x233] : 0;
    reply(CMD_GLOBAL, p, sizeof p);
}

/* a byte setting {sub, value}: set (flag 0x12) or read (0x11) */
static void byte_setting(uint32_t sub, uint32_t value, int flag, int off, int lo, int hi, int as_bool, int sign,
                         uint32_t told)
{
    load();
    if (flag == FLAG_DATA) {
        int v = sign ? (int8_t)value : (uint8_t)value;
        if (as_bool)
            v = v != 0;
        if (v < lo)
            v = lo;
        if (v > hi)
            v = hi;
        rec[off] = (uint8_t)v;
        store();
        answer(told, sign ? (uint32_t)(int32_t)(int8_t)rec[off] : rec[off]);
    } else if (flag == FLAG_REQUEST) {
        answer(sub, sign ? (uint32_t)(int32_t)(int8_t)rec[off] : rec[off]);
    }
}

static void drum_setting(uint32_t sub, int k, uint32_t value, int flag)
{
    struct globals *g = srv.globals;
    load();
    uint32_t pattern_word = (uint32_t)rd16(rec + 0x106) << 16 | rd16(rec + 0x108);
    switch (k) {
    case 1:                                 /* on / off (+0xBF0, not stored) */
        if (flag == FLAG_DATA) {
            if (drums_playing() == (value != 0))
                return;
            drums_play(value != 0);
            answer(0x8100, (uint32_t)drums_playing());
        } else if (flag == FLAG_REQUEST) {
            answer(sub, (uint32_t)drums_playing());
        }
        return;
    case 2:                                 /* genre << 16 | pattern */
    case 3:                                 /* pattern */
        if (flag == FLAG_DATA) {
            if (k == 2) {
                rec[0x106] = (uint8_t)(value >> 16);
                rec[0x107] = (uint8_t)(value >> 24);
            }
            rec[0x108] = (uint8_t)value;
            rec[0x109] = (uint8_t)(value >> 8);
            drum_user_deselect();           /* a pattern of the bank chosen: it plays */
            store();
            pattern_word = (uint32_t)rd16(rec + 0x106) << 16 | rd16(rec + 0x108);
            answer(0x8200, pattern_word);
        } else if (flag == FLAG_REQUEST) {
            answer(k == 2 ? sub : 0x8200, pattern_word);
        }
        return;
    case 4:                                 /* tempo */
        if (flag == FLAG_DATA) {
            uint8_t clock = rec[0x79];
            if (rec[0x10c] && clock != 0 && clock != 4)
                return;
            rec[0x10a] = (uint8_t)value;
            rec[0x10b] = (uint8_t)(value >> 8);
            if (rec[0x10c]) {               /* following: the effects' tempo moves too */
                if (tempo_valid() && rec[0x233]) {
                    rec[0x234] = (uint8_t)value;
                    rec[0x235] = (uint8_t)(value >> 8);
                } else {
                    rig_set_bpm(srv.rig, (int)(value & 0xffff));
                }
            }
            store();
            if (g->drum_sync && g->tempo_mode && g->bpm)
                rig_set_global_tempo(srv.rig, g->bpm);
            drums_set_bpm(rd16(rec + 0x10a));
            answer(0x8400, rd16(rec + 0x10a));
        } else if (flag == FLAG_REQUEST) {
            answer(sub, rd16(rec + 0x10a));
        }
        return;
    case 5:                                 /* follow the effects' tempo */
        if (flag == FLAG_DATA) {
            rec[0x10c] = value != 0;
            if (value) {
                uint16_t bpm = tempo_valid() && rec[0x233] ? rd16(rec + 0x234) : srv.rig->preset.bpm;
                rec[0x10a] = (uint8_t)bpm;
                rec[0x10b] = (uint8_t)(bpm >> 8);
                drums_set_bpm(bpm);
                answer(0x8400, bpm);
            }
            store();
            answer(0x8500, rec[0x10c]);
        } else if (flag == FLAG_REQUEST) {
            answer(sub, rec[0x10c]);
        }
        return;
    case 6:                                 /* volume */
        if (flag == FLAG_DATA) {
            if ((uint8_t)value > 100)
                return;
            rec[0x10d] = (uint8_t)value;
            store();
            answer(0x8600, rec[0x10d]);
        } else if (flag == FLAG_REQUEST) {
            answer(sub, rec[0x10d]);
        }
        return;
    }
}

/* the single parameters: sub = group << 12 | parameter << 8 | channel (FUN_0002CB10 and its siblings) */
static void single(uint32_t sub, uint32_t value, int flag)
{
    int group = (int)((sub & 0xffff) >> 12), k = (int)((sub & 0xfff) >> 8), c = (int)(sub & 0xff);
    switch (group) {
    case 0:
        if (k == 2) {                       /* run mode */
            if (flag == FLAG_DATA && (uint8_t)value <= 2)
                srv.run_mode = (uint8_t)value;
            else if (flag == FLAG_REQUEST)
                answer(sub, srv.run_mode);
        } else if (k == 3 && flag == FLAG_REQUEST) {
            float m = srv.master * 100.0f;
            answer(sub, (uint32_t)(m < 0 ? 0 : m > 100 ? 100 : m));
        } else if (k == 5) {
            byte_setting(sub, value, flag, 0x11, 0, 1, 1, 0, 0x500);
        } else if (k == 6) {                /* USB mode: set without an answer */
            load();
            if (flag == FLAG_DATA) {
                uint8_t v = (uint8_t)value > 1 ? 1 : (uint8_t)value;
                if (v != rec[0x230]) {
                    rec[0x230] = v;
                    store();
                }
            } else if (flag == FLAG_REQUEST) {
                answer(sub, rec[0x230]);
            }
        } else if (k == 7) {                /* "ailments": overload flag, cleared once read */
            if (flag == FLAG_DATA) {
                srv.overload = (uint8_t)value;
                if (srv.overload)
                    answer(0x700, srv.overload);
            } else if (flag == FLAG_REQUEST) {
                uint32_t v = srv.overload;
                srv.overload = 0;
                answer(sub, v);
            }
        } else if (k == 8 && flag == FLAG_REQUEST) {
            answer(sub, srv.rig->dirty);
        }
        return;
    case 1:
        if (k == 1) {
            load();
            if (flag == FLAG_DATA) {
                rec[0x12] = (uint8_t)value < LANGUAGES ? (uint8_t)value : 0;
                store();
                answer(0x1100, rec[0x12]);
            } else if (flag == FLAG_REQUEST) {
                answer(sub, rec[0x12]);
            }
        } else if (k == 3) {
            load();
            if (flag == FLAG_DATA && c < 2) {
                rec[0x14 + c] = (uint8_t)value;
                store();
                answer(0x1300, rec[0x14 + c]);
            } else if (flag == FLAG_REQUEST) {
                answer(sub, c < 2 ? rec[0x14 + c] : 0);
            }
        } else if (k == 4 && flag == FLAG_REQUEST) {
            load();
            answer(sub, rec[0x16]);
        } else if (k == 5 && flag == FLAG_REQUEST) {
            load();
            answer(sub, rd16(rec + 0x18));
        }
        return;
    case 2:
        if (k == 1)
            byte_setting(sub, value, flag, 0x1a, -20, 20, 0, 1, 0x2100);
        else if (k == 2)
            byte_setting(sub, value, flag, 0x1b, 0, 3, 0, 0, 0x2200);
        else if (k == 3 && (c < 2 || flag == FLAG_REQUEST)) {
            if (c < 2)
                byte_setting(sub, value, flag, 0x1c + c, 0, 1, 1, 0, 0x2300u | (uint32_t)c);
            else
                answer(sub, 0);
        } else if (k == 4)
            byte_setting(sub, value, flag, 0x232, 0, 100, 0, 0, 0x2400);
        else if (k == 5) {
            load();
            if (flag == FLAG_DATA) {
                rec[0x233] = value != 0;
                store();
                srv.rig->global_bpm = 0;
                rig_set_global_tempo(srv.rig, srv.globals->tempo_mode ? srv.globals->bpm : 0);
                answer(0x2500, tempo_valid() ? rec[0x233] : 0u);
            } else if (flag == FLAG_REQUEST) {
                answer(sub, tempo_valid() ? rec[0x233] : 0u);
            }
        } else if (k == 6) {                /* the global tempo itself */
            load();
            if (flag == FLAG_DATA && value >= 40 && value <= 300) {
                rec[0x234] = (uint8_t)value;
                rec[0x235] = (uint8_t)(value >> 8);
                store();
                if (srv.globals->tempo_mode)
                    rig_set_global_tempo(srv.rig, srv.globals->bpm);
                answer(0x2600, rd16(rec + 0x234));
            } else if (flag == FLAG_REQUEST) {
                answer(sub, rd16(rec + 0x234));
            }
        }
        return;
    case 3:
        if (k == 1)
            byte_setting(sub, value, flag, 0x1e, -20, 20, 0, 1, 0x3100);
        else if (k == 2)
            byte_setting(sub, value, flag, 0x1f, -20, 20, 0, 1, 0x3200);
        else if (k == 3)
            byte_setting(sub, value, flag, 0x20, 0, 1, 1, 0, 0x3300);
        else if (k == 4) {
            if (c < 2)
                byte_setting(sub, value, flag, 0x21 + c, 0, 1, 1, 0, 0x3400u | (uint32_t)c);
            else if (flag == FLAG_REQUEST)
                answer(sub, 0);
        }
        return;
    case 4:
        if ((k == 1 || k == 2) && c < 8)
            byte_setting(sub, value, flag, (k == 1 ? 0x23 : 0x25) + c, -128, 127, 0, 1,
                         (k == 1 ? 0x4100u : 0x4200u) | (uint32_t)c);
        else if (k == 2 && flag == FLAG_REQUEST)
            answer(sub, 0);
        return;
    case 5:
        if (k == 1) {
            if (c < 2)
                byte_setting(sub, value, flag, 0x2d, 0, 1, 1, 0, 0x5100u | (uint32_t)c);
            else if (flag == FLAG_REQUEST)
                answer(sub, 0);
        } else if (k == 2) {
            load();
            if (flag == FLAG_DATA && (uint8_t)value < 2) {
                rec[0x2f] = (uint8_t)value;
                store();
                answer(0x5200, rec[0x2f]);
            } else if (flag == FLAG_REQUEST) {
                answer(sub, rec[0x2f]);
            }
        }
        return;
    case 6:
        load();
        if (k == 1) {
            if (flag == FLAG_DATA && c < 4) {
                int16_t v = (int16_t)value;
                if (v > 0x10)
                    v = 0x10;
                rec[0x70 + 2 * c] = (uint8_t)v;
                rec[0x71 + 2 * c] = (uint8_t)((uint16_t)v >> 8);
                store();
                answer(0x6100u | (uint32_t)c, (uint32_t)(int32_t)(int16_t)rd16(rec + 0x70 + 2 * c));
            } else if (flag == FLAG_REQUEST) {
                answer(sub, c < 4 ? (uint32_t)(int32_t)(int16_t)rd16(rec + 0x70 + 2 * c) : 0);
            }
        } else if (k >= 2 && k <= 4) {
            static const uint8_t lim[5] = { 0, 0, 3, 5, 5 };
            int off = 0x76 + k;
            if (flag == FLAG_DATA && (uint8_t)value < lim[k]) {
                rec[off] = (uint8_t)value;
                store();
                answer((uint32_t)(0x6000 | k << 8), rec[off]);
            } else if (flag == FLAG_REQUEST) {
                answer(sub, rec[off]);
            }
        }
        return;
    case 8:
        drum_setting(sub, k, value, flag);
        return;
    }
}

void msg_global(const struct msg *m)
{
    if ((m->cmd & 0xf) == 0) {
        if (m->flag == FLAG_REQUEST)
            settings_block();
        return;
    }
    uint32_t sub = msg_word(m, 2);
    if (((sub & 0xffff) >> 12) == 9) {     /* Clone capture deletion */
        media_delete_clone(sub, msg_word(m, 3), m->flag);
        return;
    }
    single(sub, msg_word(m, 3), m->flag);
}

/* group 6: the global EQ (switch +0x7B, 31 parameters +0x7C, ranges at 0x428E4) */
void msg_global_eq(const struct msg *m)
{
    uint32_t sub = m->cmd & 0xf;
    struct globals *g = srv.globals;
    if (sub == 0) {
        if (m->flag == FLAG_REQUEST) {
            load();
            reply(CMD_EQ, rec + 0x7c, 0x7c);
        }
    } else if (sub == 1) {
        if (m->flag == FLAG_DATA) {
            int on = msg_word(m, 3) != 0;
            if (g->eq_on == on)
                return;
            g->eq_on = (uint8_t)on;
            globals_apply(g);
            globals_touch();
            notify_words(0x16000001u, 2, 0u, (uint32_t)on);
        } else if (m->flag == FLAG_REQUEST) {
            notify_words(0x16000001u, 2, 0u, (uint32_t)g->eq_on);
        }
    } else if (sub == 2) {
        uint32_t word = msg_word(m, 2), k = word & 0xff, v;
        if (m->flag == FLAG_DATA) {
            if (k >= EQ_PARAMS)
                return;
            float f = rdf32(m->data + 0x0c);
            const struct eq_param_range *r = &eq_param_ranges[k];
            if (f < r->min)
                f = r->min;
            if (f > r->max)
                f = r->max;
            g->eq[k] = f;
            globals_apply(g);
            globals_touch();
            memcpy(&v, &g->eq[k], 4);
            notify_words(0x16000002u, 2, k, v);
        } else if (m->flag == FLAG_REQUEST) {
            v = 0;
            if (k < EQ_PARAMS)
                memcpy(&v, &g->eq[k], 4);
            notify_words(0x16000002u, 2, word, v);
        }
    }
}

/* group 3: the drum machine's state {u16 on, genre, pattern, BPM, u8 follow, volume} */
void msg_drum(const struct msg *m)
{
    if (m->flag != FLAG_REQUEST)
        return;
    load();
    uint8_t p[10];
    p[0] = (uint8_t)drums_playing();
    p[1] = 0;
    memcpy(p + 2, rec + 0x106, 8);
    reply(CMD_DRUM, p, sizeof p);
}
