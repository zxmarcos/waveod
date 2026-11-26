# waveod's sources (paths relative to src/), shared by the firmware and the PC builds.

# shared by the firmware and the simulator
set(WAVEOD_APP_SOURCES
    app/app.c
    app/recovery.c
    dsp/biquad.c
    dsp/global_eq.c
    dsp/io_filter.c
    dsp/pitch.c
    dsp/rfft.c
    dsp/spectrum.c
    blocks/stereo_util.c
    blocks/gate_sc.c
    blocks/comp_sc.c
    blocks/fx_loop.c
    editor/wv_msgs.c
    editor/wv_rig_msgs.c
    editor/wv_settings_msgs.c
    editor/wv_bench_msgs.c
    editor/global_msgs.c
    editor/link.c
    editor/lzo.c
    editor/media_msgs.c
    editor/preset_msgs.c
    editor/server.c
    editor/system_msgs.c
    engine/audio_engine.c
    engine/bench.c
    engine/blocks.c
    engine/cabir.c
    engine/cabir_block.c
    engine/cabmic.c
    engine/cabmic_tables.c
    engine/chain.c
    engine/chain_tail.c
    engine/drums.c
    engine/latency.c
    engine/modulators.c
    engine/looper.c
    engine/module_loader.c
    engine/nam_block.c
    engine/overload.c
    engine/pools.c
    engine/scope.c
    nam/a2.c
    nam/rs48.c
    fw/catalog.c
    fw/globals_default.c
    fw/globals_format.c
    fw/preset_default.c
    fw/ir_fs.c
    fw/ir_store.c
    fw/library.c
    fw/preset_controls.c
    fw/preset_format.c
    fw/presets.c
    fw/record_store.c
    fw/record_store_write.c
    fw/settings_record.c
    fw/wv_preset.c
    fw/wv_store.c
    fw/wv_library.c
    fw/crc32.c
    input/input.c
    midi/midi.c
    model/assign.c
    model/drum_names.c
    model/expression.c
    model/globals.c
    model/drum_user.c
    model/param_format.c
    model/param_format_table.c
    model/control.c
    model/modulation.c
    model/rig.c
    model/settings.c
    model/sysmon.c
    model/tempo.c
    model/topology.c
    model/route.c
    model/tuner.c
    ui/screens/scr_assign.c
    ui/screens/scr_browse.c
    ui/screens/scr_chain.c
    ui/screens/scr_controls.c
    ui/screens/scr_control_map.c
    ui/screens/scr_link.c
    ui/screens/scr_target.c
    ui/screens/scr_latency.c
    ui/screens/scr_exp_cal.c
    ui/screens/scr_presets.c
    ui/screens/scr_menu.c
    ui/screens/scr_mic.c
    ui/screens/scr_drum.c
    ui/screens/scr_edit.c
    ui/screens/scr_looper.c
    ui/screens/scr_eq.c
    ui/screens/scr_preset.c
    ui/screens/scr_name.c
    ui/screens/scr_save.c
    ui/screens/scr_settings.c
    ui/screens/scr_scene.c
    ui/screens/scr_stomp.c
    ui/screens/scr_tuner.c
    ui/alert.c
    ui/boot.c
    ui/images/logo.c
    ui/images/icons.c
    ui/diag_panel.c
    ui/footswitch.c
    ui/leds.c
    ui/gesture.c
    ui/midi_control.c
    ui/theme.c
    ui/ui.c
    ui/widgets/analyzer.c
    ui/widgets/block.c
    ui/widgets/param_cell.c
    ui/widgets/row_list.c
    ui/widgets/top_bar.c
    ui/fonts/font_body_14.c
    ui/fonts/font_hero_76.c
    ui/fonts/font_label_12.c
    ui/fonts/font_letter_26.c
    ui/fonts/font_note_140.c
    ui/fonts/font_tag_12.c
    ui/fonts/font_title_40.c
    ui/fonts/font_value_20.c
    ui/fonts/font_value_26.c
    usb/usb_audio.c
    usb/usb_desc.c
    usb/usb_device.c
    usb/usb_midi.c
)

# the QME-200 only
set(WAVEOD_BOARD_SOURCES
    app/main.c
    board/qme200/board.c
    board/qme200/startup.c
    board/qme200/system.c
    board/qme200/clock.c
    board/qme200/safety.c
    board/qme200/tempmon.c
    board/qme200/time.c
    board/qme200/pins.c
    board/qme200/shift_reg.c
    board/qme200/leds.c
    board/qme200/lcd.c
    board/qme200/controls.c
    board/qme200/i2c_soft.c
    board/qme200/touch_gt911.c
    board/qme200/audio.c
    board/qme200/flash.c
    board/qme200/flexspi.c
    board/qme200/fmt.c
    board/qme200/libc.c
    board/qme200/libm.c
    board/qme200/usb_dcd.c
    board/qme200/midi_hw.c
)

# the simulator only
set(WAVEOD_HOST_SOURCES
    host/host_audio.c
    host/host_controls.c
    host/host_leds.c
    host/host_display.c
    host/host_flash.c
    host/host_midi.c
    host/host_safety.c
    host/host_system.c
    host/host_touch.c
    host/host_usb.c
    host/png.c
    host/main_host.c
)

# what the unit tests link from src/
set(WAVEOD_TESTED_SOURCES
    fw/library.c fw/record_store.c fw/record_store_write.c fw/settings_record.c dsp/pitch.c fw/preset_format.c
    fw/presets.c fw/wv_preset.c fw/wv_store.c fw/wv_library.c fw/crc32.c fw/ir_store.c fw/catalog.c
    fw/globals_default.c fw/globals_format.c fw/preset_default.c dsp/biquad.c dsp/global_eq.c dsp/io_filter.c
    engine/looper.c engine/latency.c engine/cabir.c engine/cabir_block.c engine/blocks.c engine/nam_block.c nam/a2.c
    nam/rs48.c blocks/stereo_util.c blocks/gate_sc.c blocks/comp_sc.c blocks/fx_loop.c blocks/dark_muff.c
    blocks/dark_muff_lite.c blocks/green_drive.c blocks/rodent_dist.c blocks/round_fuzz.c blocks/treble_boost.c
    blocks/chorus.c blocks/flanger.c blocks/phaser.c blocks/rotary.c blocks/vibe.c blocks/tremolo.c blocks/delay.c
    blocks/reverb.c blocks/plate.c blocks/spring.c engine/cabmic.c engine/cabmic_tables.c dsp/rfft.c dsp/spectrum.c
    engine/scope.c engine/drums.c model/drum_names.c model/drum_user.c engine/pools.c engine/chain_tail.c
    engine/module_loader.c engine/overload.c model/param_format.c model/param_format_table.c model/route.c model/rig.c
    model/control.c model/modulation.c engine/modulators.c model/expression.c fw/preset_controls.c host/host_flash.c
    editor/lzo.c fw/ir_fs.c midi/midi.c
)

# the unit tests (paths relative to tests/)
set(WAVEOD_TEST_SOURCES
    test_main.c test_formats.c test_loader.c test_store.c test_tuner.c test_overload.c test_globals.c test_assign.c
    test_route.c test_looper.c test_drums.c test_editor.c test_midi.c test_wvstore.c test_scenes.c test_controls.c
    test_latency.c test_cabir.c test_spill.c test_blocks.c test_mods.c test_drives.c test_chorus.c test_modfx.c
    test_delay.c test_reverb.c test_plate.c test_nam.c test_spectrum.c
)

# the files that show the version (cmake/version.cmake)
set(WAVEOD_VERSION_SOURCES editor/wv_msgs.c ui/screens/scr_settings.c)
