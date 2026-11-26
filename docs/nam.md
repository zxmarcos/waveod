# NAM captures in the Clone slots

waveod runs NAM captures (Neural Amp Modeler's A2-Lite network) in the Clone slots, natively: an engine block
(`engine/nam_block.c`, docs/blocks.md) picked per Clone item. A slot whose data is a NAM capture runs on it;
every other slot (the factory's Clone captures, a capture made on the pedal, an empty slot) stays on the
original's Clone module, as before.

## The capture in a slot (`nam/capture.h`)

A Clone slot keeps 0x2000 bytes. A NAM capture is a 0x88-byte header ("NAM2", version, weight count,
loudness, sample rate, CRC-32 of the weights, flags) and the network's 1871 weights (7620 bytes in all). The
header leaves the words where the original's Clone module reads its IR lengths at zero, so the original
firmware leaves such a slot inactive (the signal passes) instead of reading past it.

It is the same format matribox Studio's NAM tab writes (`matribox/usb/nam_clone.py`), byte for byte: a slot
written by either works on both firmwares (on the original only with the research builds' NAM Clone module).

Writing one: the original's own Clone command (0x18000001, what the PC editor uses for its Clone import), which
waveod answers like the original (`editor/media_msgs.c`). waveod Studio's NAM tab (`ui/namview.py`) lists the
10 slots with what each holds (waveod's command 0x1F000010, protocol 6: original capture, NAM at 44.1 kHz, NAM
at 48 kHz resampled, NAM with a broken CRC, empty; the rate, loudness and CRC of a NAM capture) and writes a
`.nam` into the selected slot, after a dialog with its name, the rate choice for a 48 kHz capture and what the
slot loses. The same from the command line:

```
python -m waveod_studio nam amp.nam --slot 4 [--name "Plexi"] [--44k]
python -m waveod_studio clone                 # the names of the 10 slots
```

## On the pedal

User IR and Clone items show the name they were stored with (the editor's or the Studio's; the IR directory's
"Clone 1".. only for slots never named). The models screen splits CLONE into two types: "NAM" (the slots
with a NAM capture) and "Capturas" (the others). A NAM row ends with its rate: "48k" and the resampler's icon
(a wave over a two-way arrow) when it runs at 48 kHz through the resampler, "44.1k" when it runs at the
pedal's rate (`catalog_clone_kind`).

`studio/waveod_studio/nam.py` takes the A2-Lite network out of a `.nam` file (a plain A2-Lite WaveNet, or the
3-channel submodel of a full A2's SlimmableContainer) and packs it. Writing a slot replaces what it held: the
user's own captures live there.

## Which engine runs a Clone item (`fw/catalog.c`)

`catalog_item` reads the item's data (`catalog_data`) and asks `blocks_clone_engine`: a NAM capture's header
(magic, version, weight count) gives the NAM block, anything else none. The weights' CRC is checked when the
block loads; a broken capture does not run (the signal passes). The chain hands the block the slot's 0x2000
bytes as `env->data`.

## The block

Knobs are the Clone entry's own (0..100, 50 = neutral), so presets, the edit screen and the PC editor are
unchanged:

| knob | what |
|---|---|
| 0 Gain | input drive, (k − 50) × 0.4 dB |
| 1 Volume | output level, (k − 50) × 0.4 dB, 0 = mute |
| 2 Bass | low shelf 100 Hz, (k − 50) × 0.24 dB |
| 3 Middle | peak 700 Hz, Q 0.7, (k − 50) × 0.24 dB |
| 4 Treble | high shelf 3 kHz, (k − 50) × 0.24 dB |

The input is the mono sum of the chain at NAM's full scale (× 0.45659: the chain's ±2.19 to NAM's ±1), the
output goes to both channels, at the capture's loudness brought to −18 dB. Gains move over a block.

**Rates.** NAM trains at 48 kHz; a 48 kHz capture runs at 48 kHz through `nam/rs48.c` unless its flag says to
run at the pedal's 44.1 kHz (`--44k`: no resampling, the capture sounds slightly brighter and its cost is
lower). Other rates run at 44.1 kHz. The network takes whole groups of 4 samples best, so the 69 or 70 samples
of a block go through two small FIFOs that keep the leftovers; the output FIFO starts with 3 samples of the
silent output: 3 samples (62 µs) more, 1.09 ms in all.

The resampler is one polyphase stage each way (160/147 up, 147/160 down) on Kaiser-windowed sinc prototypes at
7.056 MHz: 32 taps per output on the way up (pass 20 kHz, stop 28 kHz, 90 dB), 64 on the way down (pass
20 kHz, stop 24.1 kHz, 86 dB), 6336 multiply-adds per block and 1.03 ms. A tone's image is 90 dB down or more
over the audible band and the pass band is flat within 0.001 dB to 20 kHz. The prototypes are symmetric, so a
phase and its mirror share one row of the tables, read forwards or backwards: 29 KB for every instance, in the
OCRAM, the next output's row prefetched. (Until this engine: two stages each way through 73.5 kHz, 13484
multiply-adds and 1.36 ms. Its images were 150 dB down below 16 kHz, both stages' stop bands on top of each
other, far past what the converters resolve.)

The tone knobs at 50 are 0 dB: such a band is not run.

**Memory.** The network's six long histories (the dilation 101 and 239 layers, 67 KB) and the rest of the
block's state in the state pool (~104 KB, OCRAM). Its derived weights (7.9 KB), read for every frame, in the
module code pool (ITCM: no wait states, and outside the D-cache the histories keep flushing). Its 16 short
histories (30 KB) are all read and written in every block; with the long ones' 28 KB a block they are twice
the 32 KB the D-cache holds, so most of their lines would be fetched from the OCRAM again in every block. They
go to the code pool as well, while that leaves half of it (64 KB) for the modules of the slots after this one
and of the next preset (loaded beside this one's tail), and not when this preset is itself loaded beside a
tail: what fits side by side stays what it was. Otherwise they stay in the block's state (the same bits either
way, `tests/test_nam.c`).

The state pool has 192 KB, so a preset holds one NAM block (a second one is MODULE_NO_MEMORY and passes the
signal); during a preset change with the spillover, a NAM block in each preset does not fit side by side, and
the tail is cut (`model/rig.c`, as for any preset that does not fit beside the tail). Init's
`WVFX_ERR_NO_MEMORY` (sdk/wvfx.h) reports the full code pool as a full pool.

## The engine (`nam/a2.c`)

The A2-Lite network of NeuralAmpModelerCore (`NAM/wavenet/a2_fast.cpp`, MIT licence): 23 dilated convolution
layers of 3 channels (kernels 6 × 14, 15, 15, 6 × 7; dilations 1..239), LeakyReLU 0.01, a 1×1 mixing per layer
and a 16-tap head over the sum of the activations, about 1730 multiply-adds per sample. The same network,
computed with fewer operations than the core's code:

- one kernel per layer runs groups of 4 frames with the 12 accumulators in registers from the first tap to the
  residual (Cortex-M7 assembly; weights loaded with VLDM once per tap and group; the K = 15 layers step through
  their taps from one pointer);
- layer 0 sees a rank-1 input (the rechannel times the input), 3 multiply-adds per tap instead of 9, and its
  mix-in, on the same input, is added to its newest tap;
- the 1×1 biases are not added per frame: the residual stream is kept without what they have added so far (a
  constant per layer), and each layer's convolution bias takes its taps' sum times that constant, once, when
  the capture loads;
- the head's scale is folded into its weights; the head runs 4 frames per pass and moves on by one column per
  tap, loading only the new one;
- histories: linear buffers for the 16 layers with a lookback up to 205 columns, moved down as soon as their
  layer has run; rings with spare columns for the dilation 101 and 239 layers, run in one call unless a tap or
  the writer passes the ring's end in that block. The rings' five older taps are prefetched (the D-cache has
  lost them since the block before), every line of them.

Every multiply-add is fused and comes in the same order on the M7 (VFMA) and on the PC (fmaf), so the PC build
gives the same bits as the pedal's. Against NAM's own code it differs by float rounding only: −114..−128 dB on
14 real captures (the engine before this one, which added the 1×1 biases per frame: −117..−129 dB; NAM's own
float code is about −104 dB from a float64 computation). The network was ported and tuned in matribox-studio
(`nam/a2fast.c`, research/NOTES.md "NAM mais barato"); waveod's copy keeps all state in the instance.

## Cost

Instructions per 64-frame audio block in the emulator (`tools/check_nam.py`, the Clone block against the same
preset without it), and the lines the D-cache has to fetch per block (`tools/profile_nam.py`: a model of the
M7's cache, 32 KB, 4 ways, 32-byte lines, a random way replaced, over the interrupt's memory accesses in the
emulator, no prefetch; the short histories in the code pool):

| capture | the block | before this engine | line fetches | before |
|---|---|---|---|---|
| 48 kHz (resampled) | +165.6 k | +203.9 k | 1820 | 2960 |
| 44.1 kHz (`--44k`) | +141.0 k | +157.8 k | 990 | 2290 |

Of the 165.6 k at 48 kHz the network's kernels are 148 k (1887 float operations per frame in about 2120
instructions), the resampler and the block's own loops 16 k (38 k with the two-stage resampler). What is still
fetched is the rings' older taps (inherent: a block reads 28 KB of them, last seen two to four blocks ago) and
the resampler's tables; both are prefetched. The engine before
(matribox-studio's `nam/a2fast.c` with per-instance state) was itself 16-17 % under the first one
(`nam/a2lite.c`, 245.8 k / 187.9 k), which ran on the pedal at 461 us for the network at 48 kHz.

On the pedal (protocol 11, `tools/bench.py CLN:#0`, docs/bench.md; the 7 NAM captures of the Clone slots, 48 kHz
resampled, the block alone in the ITCM case): 261.1 k cycles per block (median, p99 274 k; 30.0 % of a block at
600 MHz), the engine before 307.7 k (35.3 %): -15 %. With the data cache cleaned before every block: 261.7 k, the
same.

## Verification

- `tests/test_nam.c` (host): the network against a float64 reference written from the model's definition, on
  pseudo-random weights (−133 dB); blocks of 1..72 frames give the bits of blocks of 64; the block recognises a
  capture by its header and refuses a broken CRC; at 44.1 kHz with neutral knobs it is the network × the
  loudness factor; Volume 0 mutes; at 48 kHz silence settles on the 44.1 kHz path's level and a tone comes back
  at about the same level; the short histories in the code pool, in the block's state (little room in the
  pool, or a tail beside the preset): the same bits. The resampler alone: 69..70 samples up, the FIFO at 0..2,
  45.3 samples of delay, flat within 0.005 dB to 20 kHz, the images of 1..16 kHz tones 90 dB down (up) and
  98 dB down (down).
- `tools/check_nam.py FW.bin MODEL.nam [--core]`: the whole firmware in the emulator, the capture written over
  USB like the Studio, every call of the block recorded and replayed by the PC build (`tools/nam_host.c`).

  On APP-5153-Ivory-Red-I.nam (a 48 kHz A2-Lite capture): the slots written and named over USB; at 48 kHz and at
  44.1 kHz the block loaded once, 240 calls (30720 samples, Gain and Treble moved halfway) replayed by the PC
  build bit for bit; a factory Clone slot still on the original's module. `--core`: the network against NAM's
  own code (matribox-studio's `nam/a2lite.c`, which matches NeuralAmpModelerCore bit for bit): −116.5 dB.
- `tools/profile_nam.py FW.bin MODEL.nam`: the interrupt's instructions by function and the D-cache model of
  "Cost".
- Not on the pedal yet: the first run needs a Clone slot written (ask before: the slots hold the user's
  captures).
