/* Firmware entry point (the simulator has its own main in src/host). */
#include "app/app.h"

int main(void)
{
    app_init();
    for (;;)
        app_step();
}
