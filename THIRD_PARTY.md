# Third-party code, data and tools

waveod itself is MIT licensed (`LICENSE`). These parts come from others, under their own terms.

## In this repository

| Part | Where | Author | Licence |
|---|---|---|---|
| LVGL 9.2.2 | `third_party/lvgl` (git submodule) | LVGL LLC and contributors | MIT |
| NeuralAmpModelerCore | `third_party/nam` (the Clone converter, `tools/clonefit`) | Steven Atkinson and contributors | MIT (`third_party/nam/LICENSE`) |
| Eigen | `third_party/nam/Dependencies/eigen` | the Eigen authors | MPL-2.0 (`COPYING.MPL2`, with the files under the other `COPYING.*`) |
| nlohmann/json | `third_party/nam/Dependencies/nlohmann` | Niels Lohmann | MIT |
| musl's single-precision sinf, cosf, expf, logf | `src/board/qme200/libm.c` (ported) | the musl authors (derived from FreeBSD's msun, Sun Microsystems) | MIT |
| CMSIS-DSP's real FFT, as the stock Harmony D module runs it | `src/blocks/fx/fft512.h`, `fft_tables.h` | Arm Limited | Apache-2.0 |
| Barlow Semi Condensed | `assets/fonts`, `studio/ui/src/assets/fonts` | Jeremy Tribby | SIL Open Font License 1.1 (`assets/fonts/src/OFL.txt`) |
| IBM Plex Sans and Mono | `studio/waveod_studio/fonts`, `studio/ui/src/assets/fonts` | IBM | SIL Open Font License 1.1 (`studio/waveod_studio/fonts/OFL.txt`) |
| The stock firmware update V1.2.2 | `blobs/` | Sonicake | Sonicake's (see `blobs/README.md`) |

The ports of the stock effects (`src/blocks/amps`, `drives`, `mods`, `delays`, `reverbs`, `dyn`, `fx`, `wah`,
`eq`, `clone`) and the tables generated from the stock firmware (`src/fw/globals_default.c`,
`src/fw/preset_default.c`, `src/board/qme200/board_v122.h`, the USB descriptors) are waveod's C written from the
stock firmware's machine code, so that both play and store the same; see `docs/stock_port.md`.

## Used, not included

| Tool | For | Licence |
|---|---|---|
| Arm Toolchain for Embedded | the firmware and the modules | Apache-2.0 with LLVM exceptions |
| CMake, Ninja | the builds | BSD-3-Clause, Apache-2.0 |
| Unicorn Engine, Capstone | the ARM emulator (`tools/emu.py`) and the checks | GPL-2.0, BSD-3-Clause |
| NumPy | the checks | BSD-3-Clause |
| Svelte, Vite, TypeScript | waveod Studio's interface (`studio/ui`) | MIT, MIT, Apache-2.0 |
| pywebview | waveod Studio's window (`--web`) | BSD-3-Clause |
| Ghidra | reading the stock firmware | Apache-2.0 |
