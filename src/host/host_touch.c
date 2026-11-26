/* Simulator: touch state set by the script. */
#include "host/host.h"
#include "platform/touch.h"

static struct touch_point point;

int touch_ready(void) { return 1; }
const char *touch_info(void) { return "simulator"; }
void touch_read(struct touch_point *out) { *out = point; }

void host_touch_set(int pressed, int x, int y)
{
    point.pressed = (uint8_t)(pressed != 0);
    point.x = (int16_t)x;
    point.y = (int16_t)y;
}
