/*
 * Rack Wah (WAH): the original's Rack Wah (QME-200 V1.2.2, library entry 2.2, id 0x0500000A; its code calls it the
 * Petrus Wah) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). The pedal wah of wah.h with
 * its constants: the band pass at 0.0342 + 0.1795 p rad, Q term 9 - 7.11 p, x 7.08; five fixed sections with EQ 1;
 * out 2 (band pass + 0.15 h) Volume (h the high pass's output).
 */
#include "wah.h"

static const struct sos EQ[5] = {
    { 0.7377452850341797f, -1.4390225410461426f, 0.7151064276695251f, 1.0f, -1.9340094327926636f, 0.9343867897987366f },
    { 0.7239174842834473f, -1.4390225410461426f, 0.7154055833816528f, 1.0f, -1.8276164531707764f, 0.882736325263977f },
    { 0.8462916016578674f, -1.4390225410461426f, 0.7191485166549683f, 1.0f, -1.9521199464797974f, 0.9698702096939087f },
    { 1.4390225410461426f, -0.5607813000679016f, 0.00713959988206625f, 1.0f, -0.980134129524231f, 0.03910050168633461f },
    { 1.3821154832839966f, -1.4390225410461426f, 0.08617349714040756f, 1.0f, -0.46459460258483887f,
      0.07414689660072327f },
};

static const struct wah_model MODEL = {
    { 0.9998167753219604f, -1.999633550643921f, 0.9998167753219604f,
      1.0191422700881958f, -1.9992672204971313f, 0.980857789516449f },
    0.034194208681583405f, 0.17951957881450653f, -7.110000133514404f, 7.079500198364258f, 0.15000000596046448f,
    EQ, 1,
};

static const struct wvfx_param params[] = {
    WAH_PARAMS,
    { "EQ", 0.0f, 1.0f, 1.0f, 1.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer;
    (void)env;
    wah_init(state, k);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    wah_process(&MODEL, state, io);
}

const struct wvfx_desc wah_rack_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_WAH, 0x102),
    .menu = WVFX_WAH,
    .nparams = sizeof params / sizeof params[0],
    .name = "Rack Wah",
    .category = "Wah",
    .params = params,
    .state_size = sizeof(struct wah),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
