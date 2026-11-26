/* The looper: record, play back at unity (levels 100 / 100), overdub, undo / redo, stop, erase. */
#include "engine/looper.h"
#include "test.h"
#include <math.h>
#include <string.h>

#define N      64
#define BLOCKS 100
#define LEN    (BLOCKS * N)

static int at;                              /* the loop position the next block plays (frames) */

static float sine(int i)
{
    return 0.4f * sinf(6.2831853f * 220.0f * (float)(i % LEN) / 44100.0f);
}

static float twice(int i) { return 2.0f * sine(i); }

/* `blocks` blocks with the sine as input (or silence); the largest |output - expect(position)|, leaving out the
   loop's first block (recording and playback start with a fade in, like the original's gain ramps) */
static float run(int blocks, int input, int place, float (*expect)(int))
{
    float err = 0;
    for (int b = 0; b < blocks; b++) {
        float io[2 * N];
        for (int f = 0; f < N; f++)
            io[2 * f] = io[2 * f + 1] = input ? sine(at + f) : 0.0f;
        looper_block(io, N);
        looper_process(io, N, place);
        for (int f = 0; f < N && expect && at >= N; f++) {
            float e = expect(at + f) + (input ? sine(at + f) : 0.0f);
            err = fmaxf(err, fabsf(io[2 * f] - e));
        }
        at = (at + N) % LEN;
    }
    return err;
}

void test_looper(void)
{
    struct looper_status st;
    looper_init();
    looper_set(100, 100, 0, 1);

    looper_command(LOOP_CMD_REC);
    at = 0;
    run(BLOCKS, 1, LOOP_PRE, 0);
    looper_status(&st);
    CHECK(st.state == LOOP_REC && st.pos == LEN, "recording (%u frames)", (unsigned)st.pos);

    looper_command(LOOP_CMD_PLAY);                  /* taken at the next block: the loop plays from its start */
    float err = run(BLOCKS, 0, LOOP_PRE, sine);
    looper_status(&st);
    CHECK(st.state == LOOP_PLAY && st.len == LEN, "playing a %u-frame loop", (unsigned)st.len);
    CHECK(err < 2e-4f, "plays back what went in at 100 / 100 (%g)", err);

    looper_command(LOOP_CMD_DUB);                   /* the same sine once more: the loop doubles */
    run(BLOCKS, 1, LOOP_PRE, 0);
    looper_command(LOOP_CMD_PLAY);
    err = run(BLOCKS, 0, LOOP_PRE, twice);
    CHECK(err < 4e-4f, "overdub adds (%g)", err);

    looper_undo();
    err = run(BLOCKS, 0, LOOP_PRE, sine);
    looper_status(&st);
    CHECK(err < 2e-4f && st.layer == 2, "undo takes the overdub back (%g)", err);
    looper_undo();
    err = run(BLOCKS, 0, LOOP_PRE, twice);
    CHECK(err < 4e-4f, "redo puts it back (%g)", err);

    /* an overdub after an undone one drops it: the loop is the first take plus the new one */
    looper_undo();
    run(1, 0, LOOP_PRE, 0);
    looper_command(LOOP_CMD_DUB);
    run(BLOCKS, 1, LOOP_PRE, 0);
    looper_command(LOOP_CMD_PLAY);
    err = run(2 * BLOCKS, 0, LOOP_PRE, twice);      /* twice round: the background merge has finished */
    CHECK(err < 4e-4f, "the undone overdub is dropped (%g)", err);

    looper_command(LOOP_CMD_STOP);
    run(1, 0, LOOP_PRE, 0);
    float silent = run(10, 0, LOOP_PRE, sine);            /* an error the size of the sine: nothing comes out */
    looper_status(&st);
    CHECK(st.state == LOOP_STOPPED && silent > 0.3f, "stopped: silent");
    looper_command(LOOP_CMD_CLEAR);
    run(1, 0, LOOP_PRE, 0);
    looper_status(&st);
    CHECK(st.state == LOOP_EMPTY && st.len == 0, "erased");

    /* POST is stereo; auto record waits for the guitar */
    looper_set(100, 100, 1, 0);
    looper_arm(1);
    run(5, 0, LOOP_POST, 0);
    looper_status(&st);
    CHECK(st.state == LOOP_EMPTY && st.armed && st.place == LOOP_POST && st.max == 0x792000 / 2,
          "armed and waiting (POST without undo: %u frames)", (unsigned)st.max);
    run(5, 1, LOOP_POST, 0);
    looper_status(&st);
    CHECK(st.state == LOOP_REC && !st.armed, "the guitar starts the recording");
    looper_command(LOOP_CMD_CLEAR);
    run(1, 0, LOOP_POST, 0);
}
