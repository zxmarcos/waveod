#include "model/tempo.h"
#include "model/globals.h"

int tempo_set(struct rig *r, struct globals *g, int bpm, enum tempo_from from)
{
    if (bpm < 40 || bpm > 300)
        return 0;
    int midi = from == TEMPO_MIDI_CLOCK;
    switch (g->clock_source) {
    case 0: if (midi) return 0; break;
    case 1: case 2: case 3: if (!midi) return 0; break;
    case 4: break;
    default: return 0;
    }
    if (from != TEMPO_KNOB && g->tempo_mode && g->bpm) {
        g->bpm = (uint16_t)bpm;
        rig_set_global_tempo(r, g->bpm);
        globals_touch();
    } else {
        rig_set_bpm(r, bpm);
    }
    return 1;
}
