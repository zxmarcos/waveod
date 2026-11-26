/*
 * Host unit tests.
 *
 *   build/host/waveod-tests FIRMWARE.bin [--dump modules.txt]
 *   build/host/waveod-tests --store-apply IN.bin OPS.bin OUT.bin
 *   build/host/waveod-tests --dsp-apply OPS.bin OUT.bin
 *   build/host/waveod-tests --cab-apply OPS.bin OUT.bin
 *   build/host/waveod-tests --drums-apply FIRMWARE.bin OPS.bin OUT.bin
 *   build/host/waveod-tests --irfs-apply IN.bin OPS.bin OUT.bin
 *
 * FIRMWARE.bin is any QME-200 update file (its effect library, IRs and factory presets are used).
 * --dump writes one line per library module with the hash of every loaded region, for tools/check_loader.py.
 */
#include "host/host.h"
#include "test.h"
#include <string.h>

int test_failures, test_checks;

int main(int argc, char **argv)
{
    const char *fw = 0, *dump = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--store-apply") == 0 && i + 3 < argc)
            return store_apply(argv[i + 1], argv[i + 2], argv[i + 3]);
        if (strcmp(argv[i], "--drums-apply") == 0 && i + 3 < argc)
            return drums_apply(argv[i + 1], argv[i + 2], argv[i + 3]);
        if (strcmp(argv[i], "--irfs-apply") == 0 && i + 3 < argc)
            return irfs_apply(argv[i + 1], argv[i + 2], argv[i + 3]);
        if (strcmp(argv[i], "--dsp-apply") == 0 && i + 2 < argc)
            return dsp_apply(argv[i + 1], argv[i + 2]);
        if (strcmp(argv[i], "--cab-apply") == 0 && i + 2 < argc)
            return cab_apply(argv[i + 1], argv[i + 2]);
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            dump = argv[++i];
        else
            fw = argv[i];
    }
    test_store();
    test_tuner();
    test_overload();
    test_globals();
    test_looper();
    test_editor();
    test_midi();
    test_wvstore();
    test_cabir();
    test_blocks();
    test_mods();
    test_drives();
    test_chorus();
    test_modfx();
    test_delay();
    test_reverb();
    test_plate();
    test_nam();
    test_spectrum();
    if (fw) {
        if (host_flash_load_firmware(fw) != 0) {
            printf("cannot read %s\n", fw);
            return 1;
        }
        test_formats();
        test_preset_save();
        test_wvpresets();
        test_scenes();
        test_controls();
        test_latency();
        test_assign();
        test_route();
        test_drums();
        test_loader(dump);
        test_spill();
        test_blocks_catalog();
        test_mods_rig();
    } else {
        printf("(no firmware given: library, preset and loader tests skipped)\n");
    }
    printf("%d checks, %d failed\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
