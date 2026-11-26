/* Preset assignments (quick knobs, expression, CTRL, footswitches) on the factory presets. */
#include "fw/presets.h"
#include "test.h"
#include <string.h>

void test_assign(void)
{
    static struct preset_source src;
    static struct preset p;
    presets_open(&src);
    CHECK(presets_load(&src, 0, &p) == PRESET_FACTORY, "factory preset 01A");
    CHECK(p.qk_target[0] == 2 && p.qk_target[1] == 3 && p.qk_target[2] == QK_VOLUME, "quick knobs %d %d %d",
          p.qk_target[0], p.qk_target[1], p.qk_target[2]);
    CHECK(p.exp[0][0].slot == 1 && p.exp[0][0].param == 0 && p.exp[0][0].max == 100.0f, "EXP 1A: volume block");
    CHECK(p.exp[1][0].slot == 4 && p.exp[1][0].param == 3, "EXP 1B: wah block");
    CHECK(p.slot[1].menu == 14 && p.slot[4].menu == 2, "the targets' blocks: volume (stored as menu 14), wah");
    CHECK(p.ctrl[0][2] == 1 && p.ctrl[1][3] == 1 && p.ctrl[4][2] == 1 && p.ctrl[4][9] == 1, "CTRL members");
    CHECK(p.fs[FS_STOMP][0] == FSF_CTRL1 && p.fs[FS_STOMP][6] == FSF_TAP && p.fs[FS_PRESET][0] == FSF_PRESET_A,
          "footswitch functions");
    CHECK(p.fs[FS_PRESET][FS_COMBO] == FSF_TUNER && p.fs[FS_PRESET][FS_COMBO + 3] == FSF_MODE, "combos");

    preset_unassign_slot(&p, 2);
    CHECK(p.qk_target[0] == -1 && p.ctrl[0][2] == 0 && p.ctrl[4][2] == 0 && p.ctrl[4][9] == 1,
          "removing a block forgets its assignments");
    preset_unassign_slot(&p, 1);
    CHECK(p.exp[0][0].slot == -1 && p.exp[0][0].min == 0.0f && p.exp[0][0].max == 100.0f, "expression target");

    static struct preset e;
    preset_clear(&e, "X");
    CHECK(e.qk_target[0] == -1 && e.qk_target[2] == QK_VOLUME && e.exp[2][2].slot == -1 &&
          e.fs[FS_PRESET][1] == FSF_PRESET_A, "an empty preset has the original's template assignments");
}
