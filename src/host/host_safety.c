/* Simulator: crash protection without crashes. The script can say how the "previous run" ended (--crash). */
#include "host/host.h"
#include "platform/safety.h"

volatile uint8_t safety_slot_main = SAFETY_NONE, safety_slot_audio = SAFETY_NONE;

static struct crash_info last;
static int last_valid;
int host_safety_tests;

void host_crash_set(const struct crash_info *c)
{
    last = *c;
    last_valid = 1;
}

int safety_last_crash(struct crash_info *out)
{
    *out = last;
    return last_valid;
}

void safety_arm(void) {}
void safety_feed(void) {}
void safety_disarm(void) {}
void safety_note_preset(int index) { (void)index; }
uint32_t safety_watchdog_hz(void) { return 0; }

void safety_test(enum safety_test kind)
{
    (void)kind;
    host_safety_tests++;
}
