/* The CPU overload guard against the original's rule (FUN_00021D28): three blocks over 1.25 ms in a row. */
#include "engine/overload.h"
#include "test.h"

void test_overload(void)
{
    struct overload_guard g = { 0 };
    CHECK(!overload_block(&g, 1.30f) && !overload_block(&g, 1.30f), "two slow blocks do not trip");
    CHECK(overload_block(&g, 1.30f), "the third slow block in a row trips");
    CHECK(!overload_block(&g, 1.30f), "the count starts again after tripping");

    g.slow = 0;
    overload_block(&g, 1.30f);
    overload_block(&g, 1.30f);
    CHECK(!overload_block(&g, 1.25f), "exactly 1.25 ms is not slow");
    CHECK(!overload_block(&g, 1.30f) && !overload_block(&g, 1.30f), "a fast block clears the count");
    CHECK(overload_block(&g, 2.0f), "third in a row again");
}
