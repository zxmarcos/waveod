/* Simulator: two frame buffers, the shown one is saved as PNG. */
#include "host/host.h"
#include "platform/display.h"


static uint32_t fbs[2][DISPLAY_W * DISPLAY_H];
static int shown;

uint32_t *display_fb(int index) { return fbs[index & 1]; }
void display_show(int index) { shown = index & 1; }
void display_backlight(unsigned percent) { (void)percent; }

int host_display_save_png(const char *path)
{
    return png_write_xrgb(path, fbs[shown], DISPLAY_W, DISPLAY_H);
}

const uint32_t *host_display_pixels(void)
{
    return fbs[shown];
}
