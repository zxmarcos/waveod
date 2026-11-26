# The original's effects as waveod blocks

The original firmware's effects (QME-200 V1.2.2, 179 modules) ported to readable C as waveod blocks
(`src/sdk/wvfx.h`), each giving the original's output bit for bit, as the base for improving them (the pre amp and
the power amp as separate blocks, 48 kHz, ...). The inventory of the 179 modules and what they share is in the
research repository (`research/matribox-stock/README.md`). Done: the B-Man amps (AMP › B-Man, 2 modules), the Brit amps (AMP › Brit, 9 modules), the Calif amps (AMP ›
Calif, 11 modules), the Dark amps (AMP › Dark, 2 modules), the Voks amps (AMP › Voks, 4 modules), the Bog amps (AMP › Bog, 4 modules), the Dizzy amps (AMP › Dizzy, 4
modules), the Doctor amps (AMP › Doctor, 2 modules), the Eng amps (AMP › Eng, 2 modules), the Flyman amps (AMP › Flyman, 4 modules), the Supero amps (AMP › Supero, 2 modules), the Superb amps (AMP › Superb, 2 modules), the single amps (TWD Deluxe, Jazz 120, Tanger R100, Halen 51,
A BassVT), the Dragon amps (AMP › Dragon, 3 modules), the Sol amps (AMP › Sol, 3 modules), the bass and acoustic
preamps (A BassFT, F-2Bass, AC Preamp, AC Preamp 2), all 24 drives
(DRIVE), all 23 modulation effects (MOD), all 17 delays (DELAY), all 12 reverbs (REVERB), all 14 dynamics (DYN), all 8 filter and pitch effects (FX), all 6 wahs (WAH), all 5 EQs (EQ), the Volume (VOL) and the Clone (CLONE, for the original's own captures).

## How a port is made

The C is written by hand from the module: Ghidra's decompile (`research/tools/ghidra/DecompAll.java`) gives the
structure and the names (the modules keep the names of their state variables: `numPfNrm1`, `knobTrebleOld`, ...),
the disassembly with its constants resolved (`research/matribox-stock/annot.py`) settles the arithmetic:

- the original's compiler fuses a product into the sum it feeds (VFMA) wherever its source had `a * b + c`, summing
  left to right: the first product rounded, the others fused. Ghidra prints both the same way, so every sum is read
  from the disassembly; `st_fma` / `st_fmad` (`amps/stock.h`) are VFMA on the module and `fmaf` / `fma` on the PC,
  and everything else is plain C under `-ffp-contract=off`;
- some modules compute coefficients (and a few scalings) in double from single-precision knobs; the C keeps each
  conversion where the original has it (`st_scale`, the `_d` tone stacks);
- the C library the original linked (armcc's `expf`, `exp2f`, `tanf`) is ported once (`st_expf`, `st_exp2f`,
  `st_tanf`): it rounds its products (VMLA), which plain C does;
- the knob tapers are the original's own tables (`amps/stock_tapers.h`, generated from STOCK.bin by
  `tools/gen_stock_tapers.py`, which checks that every module with a table of that name has the same one);
- the original's quirks stay, documented at the top of each file (a "low pass" that passes the signal, coefficients
  that stop one glide step short of the knob, Init and Process rounding a gain two ways, a taper that changes
  between Init and Process, a volume that only glides while bypassed). Dead code (state nothing reads) is left out.

Ghidra pitfalls met so far: a product it hoists into a temporary may still be fused, and its sums do not show the
order of the products inside (always the disassembly); it once swapped two variables that share a register (Brit
45JP's Init); capstone decodes literal pools as code, so an `IT` block there can seem to make the next real
instructions conditional (follow the branches). It also leaves out computations it cannot follow (`coprocessor_function2`,
`unaff_s16`): the Dual V/M's 3rd-order section is recomputed from max(Gain, 0.3) where Ghidra shows fixed constants,
the Star OD's stage gain has a floor of 0.04 (VMAXNM) it drops, and the IIC+ 1's FIR output x Volume is missing from
its listing. The code is laid out out of order (blocks placed after the loop and branched to): follow the jumps.

## Speed

The ports compute the original's samples bit for bit but not in its order. Measured on the pedal (`tools/bench.py`,
protocol 11, the original's defaults), the 63 amps take 37 % fewer cycles than the originals (2.39 M against 3.78 M
cycles per block, all of them), the Brit 45/50 family half. Where the time went and what took it away (`amps/amp.h`):

- the clipping stages (4 points per sample through the clipper, the 40-tap FIR): about 3/4 of a Brit 45. Their
  clippers return early at the limits and pick a side by `x > 0`; on the Cortex-M7 every float compare reaches its
  select through the FPSCR flags (VCMP, VMRS), which orders each point after the one before. The clippers now
  compute both sides and pick by x's bits with integer operations (`clip_pick`, `clip_sign`, `sign_of`; the sign bit
  and `x > 0` differ only at +0, where both sides give 0), and `expf` inside them takes its normal path without the
  special cases (`clip_expf`, `exp_negabs`, `clip_env_fast`: outside its range, which the clippers' inputs reach only
  past their limits or with huge inputs, the exact `st_expf` runs instead);
- a stage's oldest point is the previous input, which the sample before already clipped: `STAGE_BLOCK_POINTS`
  takes it from there (3 clips per sample, not 4; the block's first one clipped again, its knobs may have moved)
  where the clipper is the same for every point and every sample of the block (not the envelope stages, not where
  the current point has its own formula, not the Sardar's, whose prev is its output: it runs sample by sample);
- the models run stage by stage over the block instead of sample by sample: every filter keeps its state in
  registers over the block (`sos_block`), each knob's glide advances once per sample in the stage that uses it, and
  the stages run over the whole block (`stage_*_block`, `os8_block`, `stage_env_block`): every point of the block
  first, then the FIR of four samples interleaved (one FIR's 20 fused sums are a chain of dependent operations; on
  the pedal 2 or 8 at a time cost the same as 4, one at a time 80 % more).

Not taken: the FIR's sums in another order, or the polynomial `expf` replaced, would change the bits. A divide is
14 cycles on the M7 and not pipelined: the Supero's 8x stage (16 divisions per sample) stays the most expensive amp.

## The Brit family

`src/blocks/amps/`: `stock.h` (arithmetic, C library, glide, tapers), `amp.h` (what the families share: filter
sections in their summing orders, the clippers, the 4-points-per-sample stage and its 40-tap FIR, the 3rd-order
section and the classic tone stack), `brit.h` (the family's pieces: the tone stacks' constants, the presence shelves,
the tables two or more models share), one file per model.
Ids are `WVFX_ID(WVFX_AMP, n)` with n the low half of the original's id; the category is "Brit". Mono in, the same
signal on both outputs, as the originals.

| Block | Original | Path |
|---|---|---|
| Brit 45 (`brit45.c`) | 4.27, 0x0700002A | 3 pre-filters, 1st-order gain, low pass x 0.13, clip ±40 at 4x + FIR x 0.1, tone stack, clip ±1.5 + FIR, presence, 4 power sections |
| Brit 45+ (`brit45p.c`) | 4.28, 0x0700002B | the 45's bright channel: 3rd-order gain from Gain and Gain², 2 pre-EQ sections, the tone stack in double |
| Brit 45JP (`brit45jp.c`) | 4.29, 0x0700002C | the 45 and 45+ channels after the shared pre-filters, summed (Gain 1 = half the taper, Gain 2) |
| Brit 50 (`brit50.c`) | 4.30, 0x0700002D | 2 pre-filters x 0.15, 1st-order gain, a "low pass" that passes the signal, clip ±50 + FIR x 0.02, tone stack (double), clip ±2 + FIR, presence, 6 power sections |
| Brit 50+ (`brit50p.c`) | 4.31, 0x0700002E | the 50's bright channel: other pre-filters, 2nd-order gain, 2 pre-EQ sections |
| Brit 50JP (`brit50jp.c`) | 4.32, 0x0700002F | the 50 and 50+ channels summed, all in single precision |
| Brit SLP (`britslp.c`) | 4.33, 0x07000030 | 6 fixed sections around a 1st-order gain, tone stack, low pass, one clip ±0.2 (exponential) + FIR, a cookbook high shelf, 4 power sections; tone and presence glide per block |
| Brit 800 (`brit800.c`) | 4.34, 0x07000035 | 2 pre-filters, 3rd-order gain, pre-EQ, one asymmetric clip +2.5 / -3 + FIR, its own tone stack (with Treble² terms), Volume before the presence |
| Brit 900 (`brit900.c`) | 4.35, 0x0700004E | Gain² x 26, pre-filter, an 8x polyphase clipper (7.5224 x / (\|8 x\| + 0.1677)) and decimator, fixed EQ, its own tone stack (to t² m²), presence |

## The Calif family

`calif.h` has the family's pieces: the knobs of each line (IIC+ / IV LD, Dual, Star), the bright filter, the
presence sections, the asymmetric clippers, the IIC+'s tone stacks (single and double precision), the Star's tone
stack, gain and presence sections, its envelope-driven stages. The category is "Calif". Calif EQ (EQ menu) is not
part of it.

| Block | Original | Path |
|---|---|---|
| Calif Star CL (`calif_starcl.c`) | 4.13, 0x07000019 | pre-filter, tone stack (Bass pow4_5), 1st-order gain x 15, presence (Presence and Volume), a clip whose drive follows an envelope of the signal and Gain (M C (1 - e^(-k\|x\|)), k = 5 + 5 Gain + 15 env) + FIR, 3 power sections |
| Calif Star OD (`calif_starod.c`) | 4.14, 0x0700004A | Input's 1st-order drive x 30 before the Star CL's tone stack, gain x 25 and presence, clip 0.1 (1 - e^(-k\|x\|)) with k = 5 + 17 env, x max(1 / (1 + 3 k), 0.04), FIR; no power amp |
| Calif IIC+ 1 (`calif_iic1.c`) | 4.40, 0x07000039 | pre-filter, low pass, tone stack, bright filter (Gain), asymmetric clip ±1.3 + FIR x Volume, presence, 6 power sections |
| Calif IIC+ 2 (`calif_iic2.c`) | 4.41, 0x0700003A | the IIC+ 1 with another pre-filter, Volume at the end |
| Calif IIC+ 3 (`calif_iic3.c`) | 4.42, 0x0700003B | the IIC+ 1 with its own tone stack, the tone, bright filter and Treble taper in double; Bass, Middle at once |
| Calif IV LD 1 (`calif_ivld1.c`) | 4.43, 0x07000055 | the IIC+ tone stack x Gain, asymmetric clip ±1.5 + FIR x Volume x 5, presence shelf, 9 power sections |
| Calif IV LD 2 (`calif_ivld2.c`) | 4.44, 0x07000056 | the IV LD 1 with the IIC+ 3's tone stack and double tapers |
| Calif IV LD 3 (`calif_ivld3.c`) | 4.45, 0x07000057 | the IV LD 2 with the IIC+'s low pass, its own power amp and clip |
| Calif Dual V (`calif_dualv.c`) | 4.46, 0x07000068 | pre-filter x Gain, a 3rd-order section from max(Gain, 0.3), low pass, clip ±25 + FIR x 0.1, the classic tone stack x Volume, asymmetric clip ±3 + FIR, presence, 5 power sections |
| Calif Dual M (`calif_dualm.c`) | 4.47, 0x07000069 | the Dual V with clips ±20 and ±3 (symmetric), its own tone stack and power amp |
| Calif Bass (`calif_bass.c`) | 4.58, 0x07000077 | pre-filter, 1st-order gain, fixed peak x 0.1, clip 40 - 40 e^(-\|x\|/4) (not held at ±40) + FIR, tone stack x Volume x 0.4, 6 power sections |

Quirks kept: the Star CL's clip scales its three older points M C before the (1 - e) and the current one after; the
Star tone stacks fuse their Bass terms in Init and add them in Process, the Bass's Init groups a Middle term
otherwise; the IIC+ Presence 0 counts as 0.01 for the filter. The Star and Bass tone stacks pass nothing with Bass,
Middle and Treble at 0 (as the circuit), so their checks put Middle at 10 (30 for the Bass) in that scenario, and
the Star OD's Gain section passes nothing at Gain 0.

## The Superb family

`superb.h` holds the whole path; the CL and OD differ only in the Gain taper (Vol; the OD's 0.3 + 0.7 Vol), the
input level (x 40, x 200) and the output level (x 60.04, x 10.6). The stage is the Supero's 8x oversampled one
(`os8_block` in `amp.h`) with another clipper. The category is "Superb"; the tapers Vol, C, C25 and At are new in
`stock_tapers.h`.

| Block | Original | Path |
|---|---|---|
| Superb CL (`superb_cl.c`) | 4.11, 0x07000015 | x Gain^2 x 40, a 1st-order pre-filter, Bass and Middle sections (2nd order), 8x stage with the clipper 1.3416 v / (\|8 v\| + 0.08215), tone stack from Treble and Volume (3rd order), presence (2nd order), x Volume x 60.04 |
| Superb OD (`superb_od.c`) | 4.12, 0x07000048 | the CL with Gain 0.3 + 0.7 Vol, x 200 in, x 10.6 out |

Quirks kept: the tone stack depends on the glided Volume, recomputed while it glides and one step short of it;
Init computes it from the Volume knob before its x 0.01 (the factors x 0.01 instead, summed in another order), a
rounding apart until Treble or Volume moves. Presence's a0 is -0.0148 - C (the taper, not 1 - C).

## The Eng family

`eng.h` holds the whole path; the 120 and 120+ differ in the tone stack's constants (one `struct eng_ts` each), the
power sections and the output level. A 3rd-order pre-filter follows Gain (at least 0.3), the clipper is the power
one with a limit of 1 (`clip_pow2`, its last squaring fused), the tone stack's denominator has the Flyman's shape
without Volume, the presence (from exp2f, Presence linear, 0 counting as 0.01) takes 4 x Volume x the tone stack.
The category is "Eng".

| Block | Original | Path |
|---|---|---|
| Eng 120 (`eng_120.c`) | 4.50, 0x0700005F | pre-filter, 3rd-order pre-filter from Gain, low pass, x Gain / 2, clip +-1 + FIR, tone stack, presence, 4 power sections |
| Eng 120+ (`eng_120p.c`) | 4.51, 0x07000060 | its own tone stack constants and power sections, x 0.5 out |

Quirks kept: the gain and tone coefficients stop one glide step short of the knobs; Bass and Presence take effect
at once. The 120 compares Gain with 0.3 in double, the 120+ takes the float maximum: the same result.

## The Flyman family

`flyman.h` holds the whole path; the four models share its tables but for the PfMode section, the low pass, the
clipper's level, the presence's input level, the output level and the tone stack's precision. The tone stack
(3rd order) takes Volume as well as Bass, Middle and Treble, so it follows Volume's glide; the 2 and + 2 compute it
in double from single-precision m^2, t^2 and t b v, with n3 regrouped. The presence uses armcc's sinf and cosf
(`st_sinf`, `st_cosf` in `stock.h`, products rounded as its VMLA) and exp2f. The category is "Flyman".

| Block | Original | Path |
|---|---|---|
| Flyman 1 (`flyman_1.c`) | 4.36, 0x07000040 | 3 pre-filters, PfMode A, low pass 1, x Gain, clip +-3 + FIR, tone stack, presence on 2 x, 6 power sections, x 0.4118 |
| Flyman 2 (`flyman_2.c`) | 4.37, 0x07000041 | PfMode B, low pass 1, clip +-3, the tone stack in double, presence on 1 x, x 1.04 |
| Flyman+ 1 (`flyman_p1.c`) | 4.38, 0x0700005D | PfMode B, low pass 2, clip +-0.8, presence on 5 x, x 0.7987 x Volume |
| Flyman+ 2 (`flyman_p2.c`) | 4.39, 0x0700005E | PfMode A, low pass 2, clip +-0.8, the tone stack in double, presence on 3 x, x 1.04 |

Quirks kept: the + 1 applies Volume twice, in the tone stack and at the output; the tone coefficients stop one
glide step short of the knobs.

## The Supero family

A path unlike the other amps': no 4-points stage but an 8x oversampled one (`os8_block` in `amp.h`, the clipper in `supero.h`): each sample's 8 points
interpolated from the last 5 inputs (the stage's own 40-tap FIR as a polyphase filter, x 8), each through the soft
clipper 100500 |v| / (v^2 + 129200 |v| + 92490), the same FIR over the last 40 points back to one sample. Around
it: x 5 x Gain (A25), two fixed 1st-order pre-filters, a 3rd-order tone stack from one Tone knob (1 - A25 / B25
of the knob). The category is "Supero".

| Block | Original | Path |
|---|---|---|
| Supero 2 CL (`supero_cl.c`) | 4.5, 0x0700000F | one channel, the tone stack in double, x Volume x 9.6 (6.72 once Gain passes 0.0591) |
| Supero 2 OD (`supero_od.c`) | 4.6, 0x07000028 | two channels from the same input (Gain 1 / Tone 1, Gain 2 / Tone 2), summed, x Volume x 2.976 (2.083 once either Gain passes 0.0591) |

Quirks kept: the OD's Init sets each tone stack's d0 as 1.043 - the taper where Process uses Tone + 0.043, a
rounding apart until Tone moves. Dropped: the CL's second channel, fed with 0 and always +0 out; adding it only
turns a -0 result into +0, which the port keeps as + 0.

## The Voks family

`voks.h` has the family's pieces: the 30's pre-filters, the gain sections (the 30N's Bright pair, the 2nd-order one
of the 30TB and 15TB), the Boost and Tone Cut sections Tone Cut moves (through `st_exp2f`), the envelope. The
envelope-driven clipper they share with the Calif Star OD is in `amp.h` (`stage_env_block`). The category is "Voks".
Voks Wah (WAH menu) belongs with the wahs.

| Block | Original | Path |
|---|---|---|
| Voks 15TB (`voks_15tb.c`) | 4.7, 0x07000010 | 2 pre-filters, 2nd-order gain, a fixed section, tone stack (double, not divided), x 0.01, clip 40 - 40 e^(-30\|x\|) + FIR, a 1st-order tone cut through tan() (armcc's `tanf`, ported as `st_tanf`), 6 power sections x Volume |
| Voks 30N (`voks_30n.c`) | 4.8, 0x07000011 | x 1.1, 2 pre-filters, 1st-order gain (Bright off) or 2nd-order (on), x Volume, Boost, envelope-driven clip 0.1 (1 - e^(-k\|x\|)), k = 0.3 + 1.5 env, x max(1 / (1 + 3 k), 0.17) + FIR, Tone Cut, 3 power sections |
| Voks 30TB (`voks_30tb.c`) | 4.9, 0x07000027 | the 30N's path with a 2nd-order gain, a tone stack (Bass, Treble) that Char takes out, x Volume x 6, k = 6 + 5 env, floor 0.1, 5 power sections |
| Voks Bass (`voks_bass.c`) | 4.57, 0x07000075 | no clipping: 2 fixed sections, tone stack (Bass A25v3 x 0.9, Treble B25), x Volume x 9.7587 |

Quirks kept: the 15TB's Init reads Treble from a 101-point table at (int)knob (pow2_100), its Process from the
256-point pow2 at (int)knob, so after Init Treble glides from one to the other; its Bass, Treble and Tone Cut glide
0.004 per block, Gain and Volume only while the block is on; its clip's current point goes through
e^(-0.3 |x / 0.01|) where the others use x 30. Turning Bright (30N) or Char (30TB) clears the gain sections' state.
Ghidra missed the 30N's 0.17 and the 30TB's 0.1 floor (VMAXNM) and called the 15TB's `tanf` a plain function.

## The B-Man family

`bman.h`: the knobs (Gain, Volume, Middle and Treble glide; Bass and Presence at once), the pre-filters, the
classic tone stack's constants (`tone_brit_f`; the N and Bri share them), the envelope stage, the presence (one
shelf, `shelf_set` in `amp.h`, the Bog SV's form), the path after the gain section; each model's file has its gain
section and output level. The category is "B-Man".

| Block | Original | Path |
|---|---|---|
| B-Man N (`bman_n.c`) | 4.1, 0x07000003 | x 1.1, 2 pre-filters, 1st-order gain from Gain (pow2), tone stack, envelope-driven clip 0.15 (1 - e^(-k\|x\|)), k = 1.4 + 2 env, floor 0.1 + FIR, presence, x Volume x 76.6 |
| B-Man Bri (`bman_bri.c`) | 4.2, 0x07000024 | the N with a 2nd-order gain (`gain2_set`), its own presence constants, x 49.1 |

Quirks kept: the gain and tone coefficients stop one glide step short of the knobs, and are not recomputed while
bypassed. The envelope follows the tone stack's output, which is also the stage's input.

## The Bog family

`bog.h` has the family's pieces: the SV's envelope and presence (a shelf, `shelf_set` in `amp.h`), the SV OD's and XT's tone stack (one shape, its
constants per model in a `bog_ts`), the XT's pre-filters, peak, low pass and presence. The category is "Bog".

| Block | Original | Path |
|---|---|---|
| Bog SV CL (`bog_svcl.c`) | 4.15, 0x0700001A | x 1.1, 2 pre-filters, tone stack, x Gain (Bright off) or a 1st-order gain (on), x 5, 3 pre-amp sections x Volume, envelope-driven clip 0.05 (1 - e^(-k\|x\|)), k = 1 + env, floor 0.1 + FIR, presence, 3 power sections |
| Bog SV OD (`bog_svod.c`) | 4.16, 0x0700003D | x 1.1, 2 pre-filters, 2nd-order gain (pow3_5), low pass, a section x 0.25, clip 0.15, k = 1.5 + 1.7 env, floor 0.15 + FIR, the tone stack after the clip x Volume x 45, presence, 3 power sections |
| Bog XT Blue (`bog_xtblue.c`) | 4.17, 0x07000043 | 2 pre-filters, 1st-order gain, boost, peak, low pass, asymmetric clip ±1.5 + FIR, tone stack x Volume, presence (two shelves multiplied out), 6 power sections |
| Bog XT Red (`bog_xtred.c`) | 4.18, 0x0700006E | the XT Blue's path with a 2nd-order gain (`gain2_set`), its own boost, asymmetric clip ±0.6 (k 1/51.2 above, 3/128 below), 7 power sections |

Quirks kept: the XT's Presence 0 counts as 0.01, Init holds it there below the taper's point 5; turning the SV CL's
Bright clears its gain section's state.

## The Dark family

Two models with little in common, each in its own file (`dark_double.c`, `dark_deluxe.c`); the Double's envelope
is the B-Man's (`amp_env_run` in `amp.h`). The category is "Dark".

| Block | Original | Path |
|---|---|---|
| Dark Double (`dark_double.c`) | 4.3, 0x07000004 | x 1.1, 3 pre-filters, tone stack (Bass pow2, Middle, Treble), x Gain (pow1_5; Bright on: a 1st-order section), 2 pre-amp sections, envelope-driven clip 0.15, k = 0.5 + 3.5 env, floor 0.13 + FIR, x Volume x 45.56 |
| Dark Deluxe (`dark_deluxe.c`) | 4.4, 0x07000005 | 3 pre-filters, tone stack (Bass, Treble) x Gain (pow2), clip 5 (1 - e^(-8\|x / 10\|)) + FIR, 13 power sections, x 2.4 x Volume |

Quirks kept: both Inits compute the tone stack from the knobs before their x 0.01 (the factors x 0.01 instead; the
Deluxe also sums n3 in another order), a rounding apart from Process's until a tone knob moves. The Deluxe's Bass
and Treble glide 0.004 per block of samples, also while bypassed, the tone stack recomputed at each step, while
its Gain and Volume glide only while on; its stage clips the current point from x x 0.8 rather than from
(x / 10) x 8. Dropped: the Deluxe's envelope, which it computes and never uses.

## The Dizzy family

Two shapes, each in a header with its models as tables: `dizzy.h` (VH, VH+: two clipping stages) and
`dizzy_s.h` (VH S, VH+ S: one stage, the same tone stack in double, the gain section in double). The presence (two
shelves multiplied out, `shelves_set`) and the power clipper with its own limit and k each side (`clip_pow2`) are in
`amp.h`. The category is "Dizzy".

| Block | Original | Path |
|---|---|---|
| Dizzy VH (`dizzy_vh.c`) | 4.52, 0x07000065 | pre-filter, 1st-order gain, low pass x 0.4, clip +0.6 / -0.8 + FIR, tone stack x Volume, clip +1 / -0.8 + FIR, presence, 6 power sections x 4.8 |
| Dizzy VH S (`dizzy_vhs.c`) | 4.53, 0x07000066 | pre-filter, the VH's gain in double, low pass, clip ±1 + FIR, the tone stack in double x Volume, presence, 7 power sections x 5.268 |
| Dizzy VH+ (`dizzy_vhp.c`) | 4.54, 0x0700006A | the VH with another gain and low pass, x 0.6, clips ±1 and ±0.8, x 9 |
| Dizzy VH+ S (`dizzy_vhps.c`) | 4.55, 0x0700006B | the VH S with the VH+'s gain, another low pass, clip ±0.8, x 7 |

Quirks kept: with a limit of 1 the clipper fuses its last squaring (1 - q q); the VH's Gain, Bass and Presence take
effect at once where the S models glide Gain and Bass; the S models' Presence 0 counts as 0.01, Init holds it there
below the taper's point 5. Found the hard way: near 1, (1 - k |x|) is quantised so finely that one ulp at the clip's
input moves its output by 0.4 %, so a gain section computed in float where the original uses double showed only as a
rare, late difference.

## The Doctor family

`doctor.h`: the knobs (Gain, Tone Cut, Volume, Bass, Middle, Treble, all gliding; B25, Bass A25_0_0_8, a taper new
in `stock_tapers.h`), the input x 29.4, three pre-filters, the tone stack, the Tone Cut (1st order, divided by its
a0) and the post section; each model's file has its stage and output level. The category is "Doctor".

| Block | Original | Path |
|---|---|---|
| Doctor CL (`doctor_cl.c`) | 4.19, 0x0700001B | Gain x 0.9, clip +-1.275 (k 0.000734) on 4 points + FIR, the Tone Cut summed from b1 (`fos_run_b1_div`), x Volume x 2.82 |
| Doctor OD (`doctor_od.c`) | 4.20, 0x07000049 | Gain, the 8x oversampled stage (`os8_block`) with the clip +-1 (k 1 / 40.96) on 8 v, the Tone Cut summed from b0, x Volume x 1.032 |

Quirks kept: the tone and Tone Cut coefficients follow the glide only while it moves, so they stop one step short
of the knobs, and not while bypassed. The tone stack is passive (silent with Bass, Middle and Treble at 0).

## The Dragon family

`dragon.h`: the knobs (Gain, Volume, Bass, Middle, Treble, all gliding), the state, the CL's pre-filters and power
amp (the CL B shares them), the path after the clipping stage; each model's file has its front end. The tone stack
is the full one with Treble² terms (`tmb_tone_set` in `amp.h`, its constants per model in a `struct tmb_ts`), in
single precision or in double (`tmb_tone_set_d`: the same polynomials, n3 regrouped). The category is "Dragon".

| Block | Original | Path |
|---|---|---|
| Dragon CL (`dragon_cl.c`) | 4.21, 0x0700001F | 2 pre-filters, a high pass, 2nd-order gain from Gain (pow3 x 0.35), clip +3 / -2.8 + FIR, tone stack, 5 power sections x 80 Volume |
| Dragon CL B (`dragon_clb.c`) | 4.22, 0x0700007B | the CL without the high pass, its gain section (pow3) and tone stack in double, x 48 |
| Dragon OD (`dragon_od.c`) | 4.23, 0x0700007C | other pre-filters, x 5 Gain (pow2), a fixed 3rd-order section, low pass, clip ±1 + FIR, tone stack, its own 5 power sections x 12.853 |

Quirks kept: the gain and tone coefficients follow the glide only while it moves, so they stop one step short of
the knob, and not while bypassed (the glide goes on); the CL's Init computes the gain section from the taper with
the factors x 0.35 (Process from Gain x 0.35), a rounding apart until Gain moves.

## The Sol family

`sol.h`: the knobs (Gain, Volume, Middle and Treble glide; Bass and Presence at once), the CL's and OD's pre-filters
and tone stack constants (the same full tone stack as the Dragon's, `tmb_ts`), the presence (two shelves,
`shelves_set` / `shelves_set_x` in `amp.h`), the state. The category is "Sol".

| Block | Original | Path |
|---|---|---|
| Sol 100 CL (`sol_cl.c`) | 4.24, 0x07000023 | 2 pre-filters, x Gain (pow2_5), a fixed section, clip ±50 + FIR, the tone stack in double, x 0.2 Volume in double, presence (exponents in double), 1 power section x 0.66 |
| Sol 100 OD (`sol_od.c`) | 4.25, 0x07000047 | the CL's pre-filters, x Gain, another fixed section, low pass, clip ±10 + FIR, the tone stack, x 0.1 Volume, presence, x 1.109 (no power section) |
| Sol 100 LD (`sol_ld.c`) | 4.26, 0x07000059 | other pre-filters, 3rd-order gain from Gain, low pass, clip ±1.5 + FIR, the classic tone stack (`tone_brit_f`, the Sol's constants), x Volume, presence, 5 power sections x 1.012 |

Quirks kept: Presence is held at 0.01 below the taper's point 5, but the LD's Process holds it there only at 0, so
from points 1 to 4 its presence changes on the first sample; the CL and OD recompute it every sample there (same
result). The Dragon CL B and Sol 100 CL share one tone stack routine, the Dragon CL / OD and Sol 100 OD another:
the instructions are the same, the constants differ.

## The single amps

Five models with a family of one, each in its own file.

| Block | Original | Path |
|---|---|---|
| TWD Deluxe (`twd_deluxe.c`) | 4.0, 0x07000001 | x 1.1, 2 pre-filters, a 2nd-order Tone section (pow2_5), x Gain (pow2_5), pre-amp section, x 0.5, boost section, envelope-driven clip with a = 0.25 - 0.1 Gain, k = 5 + 6 env, floor 0.04 + FIR (pairs in `AMP_FIR_ORDER2`), x Volume x 220 |
| Jazz 120 (`jazz_120.c`) | 4.10, 0x07000014 | the tone stack in double (coefficients, state and output; Bass A25, Middle B25, Treble B25 read backwards), Bright off: x 6 Gain (A25) in double, on: a 1st-order section on 4.5 x, x 4.54; no clipping |
| Tanger R100 (`tanger_r100.c`) | 4.48, 0x07000053 | x Gain (B25) x 49, 3 pre-filters, the 8x stage (`os8_block`) with the clipper 0.9842 \|w\| / (\|w\| + 0.06552), w = 8 v, tone stack (Bass A25_0_0_8), x Volume (B25) x 1.256 |
| Halen 51 (`halen_51.c`) | 4.49, 0x0700005A | 2 pre-filters, 2nd-order gain section (pow2_5), low pass, clip +-0.5 + FIR, a tone stack with m^2, t^2 and m^2 t^2 terms, presence (`shelves_set_x`, pow4; up to point 26 it counts as 0.01) on 3 x Volume x, 4 power sections, x 0.6196 |
| A BassVT (`a_bassvt.c`) | 4.56, 0x07000073 | x Gain (B25) x 70, 2 pre-filters, a 4th-order Bass/Treble tone stack, a 3rd-order treble section (B25v2), a 3rd-order middle section (B25v3) whose form follows Midrange (0-4), x Volume x 0.63; no clipping |

Quirks kept: the TWD's stage scales a by M before 1 - e on the earlier three points and after it on the current
one; the Jazz clears its Bright section's state when Bright turns; the A BassVT updates its sections once per
block, also while bypassed, and leaves the middle section as it is for a Midrange outside 0-4; the Halen's Presence
is the knob word 6 (word 5 unused). The tapers B25v2 and B25v3 are new in `stock_tapers.h`.

## The bass and acoustic preamps

The AMP menu's last four, in `src/blocks/amps/` with the other amps (ids `WVFX_ID(WVFX_AMP, id & 0xff)`).

| Block | Original | Path |
|---|---|---|
| A BassFT (`a_bassft.c`) | 4.59, 0x08000075 | a pre-filter, a 4th-order Bass (0.06 .. 0.99) / Treble tone stack, a 2nd-order gain section from Volume (0.01 .. 0.99), half of it through the 4-points stage with 40 - 40 (1 - \|v\| / 2560)^1024 and the FIR, x 0.1173 |
| F-2Bass (`f_2bass.c`) | 4.60, 0x08000076 | 2 pre-filters, a 3rd-order tone stack (Bass A25v1, Middle, Treble B25), two sections by Bright, x 0.005877 Volume (B25); no clipping |
| AC Preamp (`ac_preamp.c`) | 4.61, 0x0800007A | `ac_preamp.h`: a shelf and a peak by Balance, blended by Tone (pow2), a peak EQ (Freq pow4, Q pow0_2, Gain), a fixed enhance section, x 4 Volume |
| AC Preamp 2 (`ac_preamp_2.c`) | 4.62, 0x0800007B | `ac_preamp.h` with another EQ frequency and Q (the ones its Init uses too) |

Quirks kept: their sections are recomputed while their knobs glide, not at the last step; their Inits compute the
coefficients from the raw knobs with the x 0.01 folded into the constants, in their own order (the F-2Bass's tone
stack sums differently from its process, the AC Preamp's Init EQ uses the AC Preamp 2's frequency and Q, kept until
the EQ knobs move); the A BassFT holds Bass and Volume in range in the process but not in the Init. The taper A25v1
is new.

## The drives

The DRIVE menu's effects (`src/blocks/drives/`), each in its own file, ids `WVFX_ID(WVFX_DRV, 0x100 + entry)`; the
shared pieces are in `drive.h` (the rational clippers `clip_rat` and `clip_cub`, the Warm OD's two-curve clipper
`clip_warm`, the Butter OD's stage `bod_*`,
3rd-order sections, the FIR order 0-3-2-1), `fuzz.h` and `skreamer.h`; `amp.h` gained `stage_block_fir_acc` (the FIR summed
onto the clean signal). Mono: the left input, the result on both outputs (the Strive crossfades each channel).

| Block | Original | Path |
|---|---|---|
| Skreamer, Skreamer 9 (`skreamer.c`, `skreamer9.c`) | 3.0, 3.1 | the same code in both modules (`skreamer.h`): 2 pre-filters, 2nd-order gain section (pow2_5), 0.012 x + 0.988 A (1 - e^(-k x)) on 4 points + FIR (order 0-3-2-1) onto half the clean signal, 2nd-order tone, x 1.65 Volume, 2 post filters, low pass |
| Butter OD (`butter_od.c`) | 3.2 | pre-filter, 1st-order gain (pow1_5), x 3, 0.005 x + 0.995 A (1 - e^-x) / (1 - e^(2 x)) (`bod_stage_run`; the current point grouped otherwise) + FIR onto the clean signal, tone, level section |
| Warm OD (`warm_od.c`) | 3.3 | 1st-order gain (pow2), a 6-range two-curve clipper blended by m(Gain), FIR, 2nd-order tone from a 64-step table, x 0.9 Volume |
| Super OD (`super_od.c`) | 3.4 | the Butter OD's stage after a 2nd-order pre-filter, 3rd-order tone (pow2), x 0.8 Volume, 4 post sections |
| Blues OD (`blues_od.c`) | 3.5 | 2nd-order gain (A25), 3rd-order section, `clip_rat` + FIR, second gain section, clip +-4 + FIR, tone (B25), post, x 0.6 Volume (A25) |
| Full OD (`full_od.c`) | 3.6 | 3rd-order gain section in double (A25, x 1.5 at Mode 0), x 1.5, rational clipper + FIR, fixed section, 1st-order tone whose curve follows Mode, x 0.49 Volume |
| Breaker OD (`breaker_od.c`) | 3.7 | pre-filter, 3rd-order gain, 1st-order section, x 6, 0.08 (1 - e^(-8 x)) + FIR, tone (pow1_5), 2 posts, x 100 Volume; Gain, Tone, Volume glide per sample |
| Gerden OD (`gerden_od.c`) | 3.8 | 2nd-order gain on the input (Gain and Voice), 1 - e^-x / 1.6 (e^(0.8 x) - 1) + FIR onto 1.5 x the input, 1st-order tone, x 0.525 Volume |
| Timmy OD (`timmy_od.c`) | 3.9 | pre-filter, 3rd-order gain (Gain, Bass), s A (1 - e^(-k x)) + FIR with s, A, k by Mode, treble, level, 3 posts, x 1.68 |
| Master OD (`master_od.c`) | 3.10 | fixed input section, 2nd-order gain (pow4), 3 fixed sections, x 0.02, 30 (1 - e^(-10 |x|)) + FIR (the current point fed the unscaled signal), a 5th-order EQ in double whose 12 coefficients are polynomials in Bass (pow4), Middle, Treble (pow0_5), level, x 0.05, 10 fixed posts, x 4.8; the gain section is recomputed only while Gain glides |
| Fuzz Cream (`fuzz_cream.c`) | 3.12 | input section (no x1 term), a high pass whose gain comes from a table by Sustain, section, the Warm OD's clipper (`clip_warm`, blended by m(Sustain)) + FIR, the same section, the clipper + FIR again, tone from a table by Tone, x 2.34 Volume (from 0, glided per process call) |
| Solar Fuzz, Red Fuzz (`solar_fuzz.c`, `red_fuzz.c`) | 3.11, 3.13 | (`fuzz.h`) input gain, filters, a hard clip on 2 points per sample with two 4-point averages, post filter, x Volume (B25) |
| JP Dist (`jp_dist.c`) | 3.14 | pre-filter x 58, clip +-4.25 + FIR, 1st-order section, 2nd-order gain, clip +-2.5 + FIR, post, a piecewise clipper + FIR, tone, x 3 Volume |
| Dark Mouse (`dark_mouse.c`) | 3.15 | 3rd-order gain (gainLine), clip +-9 + FIR, a slew limiter (1.1338 per point) + FIR (order 3-2-1-0), 3rd-order section, `clip_cub` + FIR, high pass, tone (volumeLine), x 4 Volume |
| Plexi Dist (`plexi_dist.c`) | 3.16 | 2 gain sections (level), a 5-range clipper + FIR, 3rd-order Bass/Middle/Treble tone stack, a section with no x term, x 2.88 Volume; all knobs glide per sample |
| Dist Plus (`dist_plus.c`) | 3.18 | 1st-order gain from a 64-step table, high pass, clip +-2 + FIR, band pass, high pass, piecewise clipper with a halved second FIR, 4 sections, x 1.5 Volume (from 0) |
| Shark (`shark.c`) | 3.19 | 2 gain sections (one coefficient in double), `clip_rat` + FIR, 3 fixed sections, tone, x 1.4 Volume (A25, glided per sample also bypassed) |
| Strive (`strive.c`) | 3.20 | 2 pre-filters around a 2nd-order gain (pow2_5), A (1 - e^(-k x)) + FIR by Mode, 3rd-order tone (pow0_3 at the knob's whole value), level, 5 posts; out = 2 (dry + (wet - dry) m) |
| Master Dist (`master_dist.c`) | 3.17 | 3rd-order gain, a section with no x1 term, x (4.3 Gain + 0.5), 1 - e^(-2 x) / e^(2 x) - 1 + FIR, 3rd-order Bass/Treble (pow2, pow5), section, the Contour section in double (pow0_1), level with no x1 term, 5 posts, x 13; all knobs glide per sample |
| Sardar Dist (`sardar_dist.c`) | 3.21 | input low pass, Tight (no x1 term), gain section (Gain in double), clip +-3.2 + FIR, section, x 0.5, a 5th-degree rational clipper above 0.16 + FIR, trim, presence, clip +-1.76 + FIR, treble, bass, level, x 0.52, bell, x 0.8; its stages start from their last output; out = dry + (wet - dry) m like the Strive |
| Bass OD (`bass_od.c`) | 3.22 | clean pre-filter, 1st-order gain (A25), x 1.3, the Warm OD's kind of clipper (other ranges and curves; one inlined copy folds the x 1.3 into its constants) + FIR, tone (B25), Mode's posts (0 none, 1, 2 x 0.8, other values 0), Blend with 4 x the clean signal, x 2.016 Volume |
| Bass Dist (`bass_dist.c`) | 3.23 | clean and distorted 2nd-order sections (A25); distorted: clip +-4 + FIR, section, `clip_cub` + FIR, 3rd-order post; Blend mixes; 4th-order Bass/Treble tone stack, x 1.95 Volume |

Quirks kept: the Gerden OD's Init reads Voice through pow1_5 and its Process through pow0_5; the Dist Plus and the Fuzz Cream start
their Volume at 0; the Fuzz Cream indexes its 64-step Sustain and Tone tables by (int)knob / 4, so only 26 steps are reachable; the Timmy OD and the Strive tell Mode 1 (and 2) from other non-zero values, which mix the table pairs
the original indexes; the Strive runs its whole path while bypassed and fades itself out, so a bypassed Strive puts
out twice its input; sections that follow a glide stop a step short (the Master OD's gain section too). Ghidra hid VMAXNM (Dark Mouse), VSELEQ (Full OD)
and the gain-dependent numerators of the Dark Mouse; a small reader that rebuilds the expressions of a straight run of VFP code gave the exact operation order of
the long coefficient functions (the Plexi's and Bass Dist's tone stacks, the Master OD's EQ polynomials, kept as
term tables in their fused order). New tapers: pow0_3, pow0_1, pow5, gainLine,
volumeLine, level.

## The modulation effects

The MOD menu's effects (`src/blocks/mods/`), ids `WVFX_ID(WVFX_MOD, 0x100 + entry)`, the files and their `_block`
symbols prefixed `mod_` (the waveod's own `tremolo.c`, `phaser.c` and others have the plain names). Shared pieces:
`mod.h` (the Sync divisions and the LFO rate `mod_rate`, the 1st-order allpass `ap1`, the modulated delay line
`MOD_LINE(n)` + `mod_line_run`), `trem.h` (the Sine, Triangle and Bias Trem), `chorus.h` (a 3rd-order section and
the E-Chorus / CE-Roto sections); `stock.h` gained `st_asinf` (armcc's).

| Block | Original | Path |
|---|---|---|
| E-Chorus (`mod_e_chorus.c`) | 8.0 | x/4 input section, per output + 0.9 x its last voice, 3rd-order low pass, 300-sample line (82 + 174 d (0.5 +- tri)), a low pass that follows Depth, 4th-order low pass; out 0.0667 dry + 0.68 Volume (0.85 + 0.15 LFO) voice |
| D-Chorus (`mod_d_chorus.c`) | 8.1 | input filter, two 430-sample lines on ramp LFOs (Mode picks step and depth), high passes, two 2nd-order sections with the voices crossed between the outputs; right x 1.3 in double |
| B-Chorus (`mod_b_chorus.c`) | 8.2 | high pass, 3rd-order low pass, two 300-sample lines (154 + (45.5 +- 28.97 asin(sin))) d), each through the low pass; out x + Volume wet |
| M-Chorus (`mod_m_chorus.c`) | 8.3 | three 700-sample lines on LFO phases 120 degrees apart, a bilinear low pass by Filter per voice; the mix in double |
| Flanger (`mod_flanger.c`) | 8.4 | two 250-sample lines (5 + 105 Pre Delay + LFO (13 + 119 Depth)) with Feedback; out 0.8 x + 0.5 read |
| Flanger N (`mod_flanger_n.c`) | 8.5 | the Flanger with negative feedback clipped to +-1.7; out 0.8 x + 0.7 read in double |
| Trem Jet (`mod_trem_jet.c`) | 8.6 | two 200-sample lines (44.1 + LFO width) with feedback clipped +-1.7, mix in double, x a tremolo with its own LFO |
| Bass Jet (`mod_bass_jet.c`) | 8.7 | the Flanger N with positive feedback, through a fixed low shelf per output |
| Vibrato (`mod_vibrato.c`) | 8.8 | two 250-sample lines read 10 + 111 (1 +- sin) d back, x Volume |
| BBD Roto (`mod_bbd_roto.c`) | 8.9 | two input sections, two 100-sample lines (20 + 34.5 (1 +- sin) d), each through a 4th-order band pass, a low pass and a 3rd-order section |
| CE-Roto (`mod_ce_roto.c`) | 8.10 | the E-Chorus's vibrato sibling (its sections in chorus.h; level, feedback and delay in double) |
| Phaser (`mod_phaser.c`) | 8.11 | 4 1st-order allpasses per output from +-0.0857 sin, 0.44 feedback from stage 4 into stage 2; out stage 4 + x |
| BBD Phaser (`mod_bbd_phaser.c`) | 8.12 | 4 allpasses from k = 441 (u/4 + (u/2)^4 u/4), u the triangle sweep x Depth (Init /100, Process /255) |
| Phaser ST (`mod_phaser_st.c`) | 8.13 | folded-phase triangle, tan(C + B tan(A tri)) by Color, high pass, 4 allpasses, 0.4 Color feedback; out (stage 4 + hp) / 2 |
| Pan Phaser (`mod_pan_phaser.c`) | 8.14 | tan-swept 4-stage phaser per output (feedback in double) then an auto-panner, two LFOs |
| Vibe (`mod_vibe.c`) | 8.15 | a 2nd-order and three 1st-order photocell stages per output from (0.5 +- 0.5 sin) d, 0.12 feedback |
| U-Vibe (`mod_u_vibe.c`) | 8.16 | cos LFO through a rational photocell curve, 4 1st-order stages; Mode 0 (Chorus) mixes the dry and feeds back 0.35, Mode 1 neither |
| Tremolo (`mod_tremolo.c`) | 8.17 | asin(sin) triangle clipped +-0.5; x - d x + d x (0.5 +- t) per output |
| Sine Trem (`mod_sine_trem.c`) | 8.18 | sin clipped +-0.625, fixed tone section x 2 (`trem.h`) |
| Triangle Trem (`mod_triangle_trem.c`) | 8.19 | the triangle doubled and clipped +-0.625; the left tone section sums in another order |
| Bias Trem (`mod_bias_trem.c`) | 8.20 | the sign of sin through a 50-sample moving sum, clipped +-(1 - 0.0075 Bias) |
| Detune (`mod_detune.c`) | 8.21 | a two-tap pitch shifter per input with 20-bit fixed-point phases and a window table, a low pass; Dry x left + 0.7 Wet x shifted |
| Lofi Bit (`mod_lofi_bit.c`) | 8.22 | sample-and-hold (Krush, pow3), quantizer to 2^n - 1 levels (Bit), bilinear low and high pass; Mix crossfade at 0.5 |

What they share: Rate is in Hz (x 1/44100) or, with Sync on (the knob's whole value 1), a tempo division (Rate 0-10:
4, 2, 3, 4/3, 1, 1.5, 2/3, 1/2, 3/4, 1/3, 1/4 beats); the LFO counts samples and starts over once the count
reaches one period; the rates and depths move 0.00022676 of the way from where they were at the block's start to the
knob each sample (the rates from 0). Identify every libm call per module: the Phaser ST's is tanf, the U-Vibe's
cosf, the others sinf / asinf (Ghidra names them all FUN_). Quirks kept: the BBD Phaser's Init reads Depth / 100
and its Process / 255; the E-Chorus and CE-Roto recompute their low pass only while Depth is still gliding (it
stops a step short) and start their rate from the tempo in the Init (the port does it on the first process call);
the Lofi Bit's Init sets its high pass with the scaling folded otherwise; the Detune reads a step table by the
Detune knob unchecked (the scenarios hold it in -50..50).

## The delays

The DELAY menu's effects (`src/blocks/delays/`), ids `WVFX_ID(WVFX_DLY, 0x100 + entry)`, the files and their
`_block` symbols prefixed `dly_` (the waveod's own `delay.c` has the plain name). Shared pieces: `dly.h` (the Sync
divisions, the delay time from Time or the tempo in float, in double (`dly_time_d`) and unclipped (`dly_time_free`,
BBD and Ice), the 4 s line `dly_line` with `dly_write` / `dly_read` (the read between two samples), the +-2 clip);
`amp.h` gained the 3rd-order section `lp3` (moved from `chorus.h`) and the tapers the `repeat` table (999 Echo). The
original's lines sit at a fixed SDRAM address; the ports keep them in the block's buffer, laid out the same way where
a module reads past one line into the next (the Reverse).

| Block | Original | Path |
|---|---|---|
| Warm (`dly_warm.c`) | 9.0 | input + Feedback x the 1st low pass's output, clipped +-1.7, two low passes in series (the 2nd's output the wet); time in double |
| Pure (`dly_pure.c`) | 9.1 | input + Feedback x read, clipped +-2; out x + 2 Mix wet / wet + 2 (1 - Mix) x |
| Mag (`dly_mag.c`) | 9.2 | a 618-sample wow line (352 + 265 \|sin\|) averaged with the input, a low pass and a high pass on the repeat |
| Tube (`dly_tube.c`) | 9.3 | feedback from the raw read, two low passes in series; out x + Mix wet / wet / 2 + 2 (1 - Mix) x |
| BBD (`dly_bbd.c`) | 9.4 | 7-pole anti-alias filter (0.75 Feedback x last output in), x w into the line, x - 0.0060 x^2 - 0.031 x^3, two reconstruction filters; the right line Time R% x Time back with a compensated feedback; Spread crossfades the outputs |
| Ping Pong (`dly_ping_pong.c`) | 9.5 | the lines' outputs crossed; low and high pass with a reciprocal a0 |
| Slapback (`dly_slapback.c`) | 9.6 | a 13230-sample line plus an 80-sample pre-ring read 30 back; time trunc(Time) x 44.1 clipped 882 .. 13230 |
| Sweep (`dly_sweep.c`) | 9.7 | the read through a tan band pass (Q 5) swept 100 .. 3000 Hz by a triangle (free or synced), x 4 |
| Ring (`dly_ring.c`) | 9.8 | the read ring modulated by cos (100 + 2000 Freq^3 Hz, phase kept when Freq moves), a 2nd-order low pass by Tone |
| Multi Tape (`dly_multi_tape.c`) | 9.9 | mono: four heads at 1/4 .. 4/4 of Time through the Tone low pass, mixed by Mode (12 ways); that, 351 samples later, through a 133-sample wow line into a second line read Time / 2 back (the repeat, fed back); wet = heads + repeat / 2 |
| Sweet (`dly_sweet.c`) | 9.10 | mono: input filter, a 3rd-order low pass into the line, a rational saturation 4x oversampled with a 40-tap FIR, a low pass that darkens with the time, 3rd- and 2nd-order low passes, a filter on the output |
| 999 Echo (`dly_999_echo.c`) | 9.11 | time-dependent 1st-order low passes, `lp3` sections, clip +-1.7, a section on each output; Feedback by the `repeat` taper |
| Rack (`dly_rack.c`) | 9.12 | a 257-sample modulated line before the delay, a tan low pass by Tone, held every 2 samples and quantized to 12 bits |
| Lo-Fi (`dly_lo_fi.c`) | 9.13 | the read through a sample-and-hold (Crush, pow3) and a quantizer to 2^n - 1 levels (Bit), a 2nd-order filter |
| Reverse (`dly_reverse.c`) | 9.14 | written backwards into rings of two segments of the time (256-sample fades), read forwards, then a feedback line |
| Eko D (`dly_eko_d.c`) | 9.15 | two delays, A on the left and B on the right, each with its own knobs; feedback x 0.9 |
| Ice Delay (`dly_ice.c`) | 9.16 | the read Time + 100 Depth (0.5 + 0.5 sin) back, 1st-order low and high pass by High / Low Cut, Phase Reverse flipping outputs; the input fades in and out with the bypass |

What they share: Time in ms x 44.1 (or 44.100002 / 44.099998 by module), or with Sync on (the knob's whole value 1)
a tempo division (Time 0-10: 4, 2, 3, 4/3, 1, 1.5, 2/3, 1/2, 3/4, 1/3, 1/4 beats), clipped to 20 ms .. 4 s; the
times, mixes and feedbacks move 0.00022676 (or 0.0002267) of the way from the block's start to the knob each sample
(a few by 0.0001 per sample), the time jumps with Sync on. Bypassed, Trail 1 lets the lines ring on their feedback
alone (the tail added to the dry), Trail 0 clears the lines once and passes the signal; each module's clear resets
its own set (indices, filters, modulators or none of those): read in the disassembly, Ghidra shows the clear falling
through into the processing where the code jumps over it. The Init computes the time from the tempo: the ports do it
on the first process call, from the Init's knobs. Ghidra hid the Ring's time glide (it shows the time fixed), the
BBD's right-channel feedback (|Feedback| + e^(-4 R^2) (1 - |Feedback|), at most 0.5 below R 0.6), the Lo-Fi's Bit
step (folded into one VFMS) and the Multi Tape's wow wrap (in double); identify each module's libm call (the Ring's
is cosf, the Ice's and Multi Tape's sinf, the tone sections' tanf, the BBD's expf). Quirks kept: the Ring glides a
copy of Ring Mix it never reads (the knob acts at once); the Ice sets its filters to 20 Hz / 20 kHz in the Init
whatever the cuts, clears its time too (it glides up from 0 after a clear) and flips the left output twice per
sample with Phase Reverse 1; the BBD's unsynced time is not clipped; the Multi Tape and Sweet are mono (the left
input); for Trail values other than 0 and 1 the Multi Tape leaves its last output in the buffer, the ports pass the
signal.

## The reverbs

The REVERB menu's effects (`src/blocks/reverbs/`), ids `WVFX_ID(WVFX_RVB, 0x100 + entry)`, the files and their
`_block` symbols prefixed `rvb_` (the waveod's own `reverb.c`, `plate.c` and `spring.c` have the plain names). Two
engines carry ten of them, each model giving its constants:

- `rvb_fdn.h`, a 4-line FDN (Room, Hall, Church, Plate, Spring, Sky, Sea, Mod Reverb, Shimmer): the input through a
  9000-sample pre-delay line modulated by the 2049-point sine table (`rvb_sine.h`, the Init's `sin()` in double,
  generated by `tools/gen_stock_sine.py`), four 4100-sample lines fed through a sparse +-0.7071 Decay matrix (the
  Spring: the full 4 x 4 Hadamard, +-Decay / 2) and clipped to +-1.7, each read moved toward its previous read by the
  length's fraction, a 2nd-order damping per line (or on the two output sums: Mod Reverb, Shimmer), a low pass per
  output (none on the Shimmer). The damping is fixed, or a shelf from 2^x set by High Damp (Plate), Tone (Spring) or
  Lo / Hi End (Mod Reverb, Shimmer); Pre Delay a knob, fixed or absent;
- `rvb_studio.h` (Studio, Club): a 14000-sample pre-delay modulated by sinf, four early taps of a 13000-sample tap line
  (x g^length), four 13000-sample lines with -g^length (g = 2^(-9.97 / (66150 Decay + 22050)), the original's
  integer power by squaring) through a damping section (poles first) and a Hadamard feedback / 2, the low pass input
  mixed from pre-delay, late and early parts by model, a low and a high pass, two allpasses per channel, x 0.7.

The Haze (`rvb_haze.c`) is an 8-line FDN in the Studio's frame (the 8 x 8 Hadamard +-(0.3 + 0.145 Decay), 10000-sample
lines, the lines' reads x 0.025 back into the pre-delay); the Shimmer adds an octave-up shifter (two 20-bit
fixed-point taps over a 4096-sample line, windowed by the module's sine curve table, as the Detune's) in its input.

| Block | Original | Path |
|---|---|---|
| Studio (`rvb_studio.c`) | 10.0 | taps 1493, 1094, 567, 5; lines 3630, 2964, 2566, 2295; allpasses 276, 1045 / 252, 968; low pass fed pre + 0.5 late + 0.3 early |
| Club (`rvb_club.c`) | 10.1 | taps 1971, 1444, 748, 5; lines 4797, 3916, 3392, 3033; allpasses 373, 1546 / 295, 1292; low pass fed 0.5 (pre + late) + 0.1 early |
| Room (`rvb_room.c`) | 10.2 | lines 1369, 1849, 3125, 2809; Decay 0.75 pow0_5 |
| Hall (`rvb_hall.c`) | 10.3 | lines 2187, 2401, 3125, 4096; Decay 0.4 + 0.55 pow0_5; left sum x 0.9 |
| Church (`rvb_church.c`) | 10.4 | the Hall with its own damping and low pass |
| Plate (`rvb_plate.c`) | 10.5 | lines 2048, 2401, 3125, 3481; Pre Delay 0.5; damping shelf by High Damp |
| Spring (`rvb_spring.c`) | 10.6 | full Hadamard matrix; lines 2048, 2187, 3125, 2401; no pre-delay; damping shelf by Tone |
| Sky (`rvb_sky.c`) | 10.7 | lines 2048, 2187, 2401, 3125; Pre Delay 0.75; depth 0.5; left sum x 0.73 |
| Sea (`rvb_sea.c`) | 10.8 | the Sky's lines; depth 1; Decay 0.5 + 0.4 pow0_5; left sum x 0.75 |
| Mod Reverb (`rvb_mod_reverb.c`) | 10.9 | the Sky's lines; damping on the sums (Lo / Hi End shelves), left x 0.8 |
| Shimmer (`rvb_shimmer.c`) | 10.10 | the Mod Reverb's FDN + an octave-up shifter fed by the outputs; no low pass; left 0.8 / 1.6 in the mix |
| Haze (`rvb_haze.c`) | 10.11 | 8 lines 3114 .. 4793; the lines' reads x 0.025 into the pre-delay; Mod 0.3 + 0.007 Mod |

What they share: Mix (and Pre Delay where it glides) move 0.00022676 of the way per sample or 0.0001 per sample, the
Studio types' Decay, Low and Hi Damp 0.003 per process call; Trail as the delays'. Quirks kept: the Studio types
recompute their gains and filters only while a knob still glides, so the last step is never applied, and keep the
early taps at the Init's Decay; the Studio, Club and Haze reset the pre-delay to the Init's Pre Delay on a clear;
their Inits (and the Plate's) set Low Damp / High Damp from the raw knob with the x 0.01 folded into the constants;
the Haze's Init reads Decay's taper at the knob's value instead of x 2.55; the Room's Init sets the matrix from
pow0_5 x 0.530325 (0.75 x 0.7071 folded), kept until Decay moves; the Haze's matrix rows 4-7 sum onto a 0.0. Ghidra
hid the Studio's clear of its indices and filters and the Shimmer's damping exponents; its sums of the Hadamard
outputs are in the asm's order.

## The dynamics

The DYN menu's effects (`src/blocks/dyn/`), ids `WVFX_ID(WVFX_DYN, 0x100 + entry)`, the files and their `_block`
symbols prefixed `dyn_`: three compressors, six boosts, two acoustic filters and three gates. All mono (the left
input; both outputs the same) and single precision, except the AC-Boost's double parts. Shared pieces: `gate.h`
(Gate 1 and 2); the boosts use the amps' 4-points stage and 40-tap FIR (`amp.h`). `stock.h` gained the C library's
`powf` (Gate 3) and `log10f` (the compressors), and `st_round12`. The tapers gained `pow0_2`, `pow0_7`, `Clip`, `mid`,
`BassLine`, `VolumeLine` (`st_volumeline_fet`: not the Dark Mouse's `volumeLine`), `Body`, `Top` and `Vol_0`. The AC
Woody's filter tables are generated from the original's module by `tools/gen_stock_peaks.py` (`dyn_woody.h`).

| Block | Original | Path |
|---|---|---|
| COMP 1 (`dyn_comp_1.c`) | 0.0 | level 20 log10 \|x (3 + 97 pow4(Sustain))\|, the gain holds the output near -10 dB (dB moving 1 - 0.98353 falling, 1 - 0.99979 rising), two 2nd-order sections, x (5 + 50 s) Volume |
| COMP 2 (`dyn_comp_2.c`) | 0.1 | COMP 1 with the detector through a 3rd-order filter by Clipping, the times by Attack, x (4 + 40 s) Volume |
| COMP 3 (`dyn_comp_3.c`) | 0.2 | threshold / ratio (1 + 0.19 Ratio) in dB, 10-90 % attack and release, Blend of dry and compressed, x Volume, a 2nd-order Tone section |
| M-Boost (`dyn_m_boost.c`) | 0.3 | two 2nd-order sections, x pow3(Gain), the stage with 3 - 3 e^(-2.5 v) / 3.5 e^(2 v) - 3.5 |
| E-Boost (`dyn_e_boost.c`) | 0.4 | a 1st-order section by Gain (C25), the stage with a rational clipper, 1st-order Bright and +3dB sections by switch |
| AC-Boost (`dyn_ac_boost.c`) | 0.5 | 1.1 x through a section, a gain section (pow0_5), the stage with 0.02 v + k 0.98 (1 - e^(-1.4 \|v\|)) summed onto half the input, a 5th-order Bass / Treble section (in double), five sections |
| BB-Boost (`dyn_bb_boost.c`) | 0.6 | a section, a gain section (pow2), the stage with L (1 - e^(-2 \|v\|)) summed onto 0.4 x, two sections, a 4th-order Bass / Treble section, a low pass, x 0.4 Volume |
| RC-Boost (`dyn_rc_boost.c`) | 0.7 | a gain section (b1 = 0), the stage with 0.02 v + k 0.98 (1 - e^(-0.9 \|v\|)) summed onto x / 2, a 4th-order Bass / Treble section, x 0.65 Volume |
| Fat Boost (`dyn_fat_boost.c`) | 0.8 | a gain section by Volume (VolumeLine; 1st order, 2nd with Low Cut), the stage held at +-4, a 4th-order Bass (BassLine) / Treble (B25) section; a fade on Low Cut changes |
| AC Woody (`dyn_ac_woody.c`) | 0.9 | eleven peak sections (five pairs and one), each at its own point of a 63-point table picked by Shape |
| AC Sim (`dyn_ac_sim.c`) | 0.10 | 29 fixed sections picked by Mode (0-3): a top path x (G1 + G0 Top), a mid band x (1.315 - 1.052 Top), a body path of three branches x (0.9999 Body + 0.0001), x Volume; a fade on Mode changes |
| Gate 1 (`dyn_gate_1.c`) | 0.11 | `gate.h` keyed by the chain's input, attack 0.9835, release 0.9995 |
| Gate 2 (`dyn_gate_2.c`) | 0.12 | `gate.h` with Attack 1 .. 20 ms, Release 10 .. 200 ms |
| Gate 3 (`dyn_gate_3.c`) | 0.13 | a downward expander keyed by the chain's input: hysteresis 3 dB, Hold, the gain (e / threshold)^(r - r state) once per block, ramped over the block |

What they share: the knobs glide 0.0001 per sample (COMP 3's Volume and Blend and the BB-Boost's Volume 0.005 per
process call, bypassed too); the boosts' tone sections are polynomials in the Bass and Treble tapers (or the knobs x
0.01), recomputed while Treble glides or Bass changes (Bass jumps). Quirks kept: the coefficients follow a glide
only while it has not reached the knob, so the last step is never applied (COMP 2, the boosts' tone sections, the
Fat Boost's gain section); the E-Boost keeps the FIR's output negated as the Bright section's past input (its output
negated) and keeps its last sections for switch values other than 0 and 1; the AC Sim's section 17 has a1 = 0 (its
Init stores 0 over it), and the sections a Mode does not use keep their state; the AC Sim and Fat Boost scale the
input by the fade's level of the sample before. Ghidra hid the BB-Boost's Volume step (once per call); its constants
for the AC Sim agree with the Init run in Unicorn, which the port's constants come from, but the asm annotator shows
a stale low half for section 17's a1.

## The filter and pitch effects

The FX menu's effects (`src/blocks/fx/`), ids `WVFX_ID(WVFX_FX, 0x100 + entry)`, the files and their `_block`
symbols prefixed `fx_`. All mono (the left input; both outputs the same) and single precision. Shared piece:
`shift.h`, the pitch shifters' window (the original's `sinCurve`, the same table in every module, moved out of the
Shimmer), the semitone steps (`powexcel`, 1 - 2^(s / 12) to 7 decimals), the 20-bit phase step and wrap and the read
between two samples in the two orders the modules fuse it. The lines live in the block's buffer (SDRAM). The tapers
gained `B` (Ring Mod's Tone); the SDK runtime gained `__aeabi_memmove4/8`.

| Block | Original | Path |
|---|---|---|
| Filter (`fx_filter.c`) | 1.0 | a fixed high pass, then a band pass (RBJ, x 7.08) stepping through Step 1-4 by a counter at the rate (Hz or Sync) |
| Octaver (`fx_octaver.c`) | 1.1 | a 4096-sample line, three taps +128 per sample (an octave down) and two -256 (up), windowed; up through a low pass; Dry x + Low Oct down + High Oct up |
| Dual Melody (`fx_dual_melody.c`) | 1.2 | two voices (Hi Pitch 0..24, Low Pitch -24..0 semitones), each its own line and two taps, a low pass; Dry x + 0.7 (Hi Vol hi + Low Vol lo) |
| Pitch (`fx_pitch.c`) | 1.3 | one line, two taps stepping from Low Pitch to Hi Pitch by Range, a low pass; Dry x + 0.7 Wet y |
| Harmony D (`fx_harmony_d.c`) | 1.4 | the period every 4 calls from the autocorrelation (512-point real FFT of the 4x decimated input, its power, the inverse; the peak at lags 8 .. 157 refined on Akima cubics), the note in the key (Mode, Key) picks two shifts (scale tables by Interval, Smooth interpolates), two 2210-sample shifters with sine-weighted taps and a low pass; mixed by Mix (dry from both channels) |
| Pitch S (`fx_pitch_s.c`) | 1.5 | a 4000-sample buffer read at 2^(s / 12) per sample, the period found from zero crossings marked by large peaks (110-sample differences every 64 samples), jumps of a period crossfaded by half a Hann window, onsets from an envelope; x + Mix (Level y - x) |
| Ring Mod (`fx_ring_mod.c`) | 1.6 | x cos(2 pi f j) (f by Freq^3 and Fine), a 1st-order low pass by Tone, mixed in by Mix |
| Tape Mod (`fx_tape_mod.c`) | 1.7 | pre-emphasis, the 4-points stage with 1 - (1 + k \|v\|)^1024 (Saturation) and the FIR, de-emphasis, a Butterworth High Cut (tanf), mixed in by Mix, x 2 Volume |

The Harmony D's FFT is CMSIS-DSP's `arm_rfft_fast_f32` for 512 points as the module runs it (`fft512.h`: the
256-point complex FFT as radix 8 by 4, the bit reversal, the real split), each line in the module's operation order
(the compiler fused some products, not by a rule: per line, read from the module); its twiddle and bit-reversal
tables are generated from the module by `tools/gen_stock_fft.py` (`fft_tables.h`). Checked alone against the
module's functions in Unicorn (forward and inverse, complex and real, random inputs): bit for bit.

What they share: the knobs act at once (the pitch shifters) or glide 0.0001 per sample (Ring Mod's Mix, Tape Mod's
three, bypassed too); Pitch S's Level and Mix glide 0.001 per sample by block. Quirks kept: the Filter holds the
step of the sample before outside its four, and at a block's first sample that is the rate's ramp step; the Ring
Mod's Init sets the carrier from Freq alone (Fine left out), the first block rescales its phase (0); Pitch S
searches the peaks once per block and glides a copy of the shift it never reads (dropped). Ghidra named the libm
calls FUN_ (identified by their polynomials: the Ring Mod's carrier is cosf, not sinf), lost Pitch S's period
acceptance (2, 3 or 4 times the last period is refused outside an unlock) and showed its read moving back
unconditionally on a buffer move (it moves only from 256 on).

## The wahs

The WAH menu's effects (`src/blocks/wah/`), ids `WVFX_ID(WVFX_WAH, 0x100 + entry)`, the files and their `_block`
symbols prefixed `wah_`. All mono (the left input; both outputs the same) and single precision. The four pedal wahs
share `wah.h` (a fixed high pass, an RBJ band pass whose frequency and Q follow Position x the range, the dry part
added, x Volume; each model its constants); Touch and Auto Wah are their own.

| Block | Original | Path |
|---|---|---|
| Voks Wah (`wah_voks.c`) | 2.0 | `wah.h`: w = 0.0627 + 0.2622 p rad, Q term 9 - 7.2 p, x 7.499; dry 0.34 x |
| Cry Wah (`wah_cry.c`) | 2.1 | w = 0.0598 + 0.2394 p, 9 - 7.11 p, x 7.08; dry 0.375 x |
| Rack Wah (`wah_rack.c`) | 2.2 | w = 0.0342 + 0.1795 p, 9 - 7.11 p, x 7.08; five fixed sections with EQ 1; out 2 (y + 0.15 h) Volume, h the high pass's output |
| Bass Wah (`wah_bass.c`) | 2.3 | w = 0.0285 + 0.1852 p, 9 - 7.47 p, x 7.943; dry 0.36 x |
| Touch Wah (`wah_touch.c`) | 2.4 | an envelope (Sense \|x\|, 0.000227 per sample either way) through a two-slope curve sweeps the band pass 250 (100) .. 1600 (500) + 10 Range Hz; x + Mix (y - x) |
| Auto Wah (`wah_auto.c`) | 2.5 | a triangle (Rate or Sync) sweeps a bilinear (tanf) band pass 200 + 3 Low .. + Depth (800 - 3 Low + 40 High) Hz; 5 Volume y |

What they share: p = Position x (Range x 0.004 below 50, 0.016 Range - 0.6 from it); the pedal wahs' Q, Volume and
Position and the Touch Wah's Mix glide 0.0001 per sample, bypassed too; the Auto Wah's rate, Depth and Volume
0.00022676 of the way from the block's start. Quirks kept: the Auto Wah sweeps by Depth at the knob (it glides a copy
it does not read); the Rack Wah's EQ and the Touch Wah's Mode count only at exactly 1. Ghidra hid the pedal wahs'
two-slope range and the Touch Wah's Mode-dependent top (500 or 1600 Hz).

## The EQs

The EQ menu's effects (`src/blocks/eq/`), ids `WVFX_ID(WVFX_EQ, 0x100 + entry)`, the files and their `_block`
symbols prefixed `eq_`. All mono (the left input; both outputs the same) and single precision (the Calif EQ's two
Qs in double). Bands -50 .. 50.

| Block | Original | Path |
|---|---|---|
| Guitar EQ 1 (`eq_guitar_1.c`) | 7.0 | `eq5.h`: five peak sections, A = 2^(0.01993 knob), m = c A, d = c / A (b0 1 + m, b2 1 - m, a0 1 + d, a2 1 - d, b1 = a1 fixed); x 2 Volume |
| Guitar EQ 2 (`eq_guitar_2.c`) | 7.1 | each band's section twice, read from a 63-point table at (int)(31 + 0.62 knob) (`eq_guitar_2_tab.h`); x 2 Volume |
| Bass EQ 1 (`eq_bass_1.c`) | 7.2 | `eq5.h` with its bands |
| Bass EQ 2 (`eq_bass_2.c`) | 7.3 | `eq5.h` with its bands |
| Calif EQ (`eq_calif.c`) | 7.4 | five peak sections from v = 0.5 + knob / 100: A = 2^(g v - g / 2), the upper three with their frequency and Q following v; no level |

The Guitar EQ 2's tables come from the module with the AC Woody's (`tools/gen_stock_peaks.py`, was
gen_stock_woody.py). What they share: a band is recomputed at the first sample after its knob changed (the module
compares the knob x 0.01 with the last one); Volume glides 0.005 per process call, bypassed too.

## Checking

`tools/check_stock.py STOCK.bin [blocks] [--cost]` runs each block's scenarios (defaults, extremes, tone settings,
+12 dB in, Presence at its taper's point 3, Gain 2 alone on the JP models, and one that moves every knob and bypasses a stretch) through the
original's module in Unicorn (matribox effemu: the original's loader and ABI), the block as a module
(`tools/wvfx_emu.py`) and the block for the PC (`tools/circuits/wvfx_host.c`, given the knob words by index in
`WVFX_WORDS`, as the module gets them), on the guitar DI, with a 500 ms beat (the original's
Process and Init both get the tempo word). Pass: the three
outputs identical in every sample of both channels (and not silent). A float-level change in a port (one FMA
written as a product and a sum) fails it; double-level changes that leave the float coefficients the same do not
show, by nature.

Result: all 174 blocks (the Clone with four captures, the Volume, 2 B-Man, 9 Brit, 11 Calif, 2 Dark, 2 Doctor, 2 Eng, 4 Flyman, 5 single amps, 4 bass and acoustic preamps, 2 Supero, 2 Superb, 4 Voks, 4 Bog, 4 Dizzy, 3 Dragon, 3 Sol, 24 drives, 23 modulation effects, 17 delays, 12 reverbs, 14 dynamics, 8 filter and pitch effects, 6 wahs, 5 EQs) bit for bit in all their scenarios, module and PC.

The other 5 modules are not ported, and need not be: FX Loop, SND and RTN run on waveod's own block in place
of the original's modules (`blocks/fx_loop.c`, docs/blocks.md: the same knobs and levels, the gains ramped), the
Cab and User IR items on waveod's Cab / IR engine (docs/cab.md). The Clone items' NAM captures play on waveod's
NAM engine (docs/nam.md), the original's own captures on the Clone block.

### The Clone

`blocks/clone/clone.c` (FFTs in `clone_fft.h`, tables from the module by `tools/gen_clone_tables.py`) plays a
capture of the original's format, the Wiener-Hammerstein model the PC editor fits to a .nam (research/NOTES.md
"Clone"): two double-precision IIRs, two uniformly partitioned convolutions (CMSIS's 128-point FFTs: the library
every module links, its radix-8 butterfly the Harmony D's, fft512.h; the radix 8 by 2 pass read from the Clone),
an exponential saturation 4x oversampled through half-band all-pass resamplers, three tone biquads and a bypass
ramp. It reads the item's data only in its init (the partitions' spectra and the model are copied into its 65 KB
of state), so the audio never reads the flash. `tools/check_stock.py clone --data CAPTURE.bin ...` runs its
scenarios once per capture (0x2000 bytes, as the Clone file system stores them; none is in the repository): four
captures read from a pedal's flash, 28 scenarios bit for bit, module and PC. Not kept: what the original does with
a capture it accepts but cannot set up (+0x12 / +0x13 not 0: it plays an empty model and writes a spectrum below
its delay lines every block; here silence), more than 16 partitions in IR 1 (it writes past its room) and reads
past the 0x2000 bytes (here zeros).

Cost (`--cost`: instructions per 64-frame block at the defaults, Unicorn):

| | 45 | 45+ | 45JP | 50 | 50+ | 50JP | SLP | 800 | 900 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| original | 77.2 k | 81.9 k | 86.5 k | 80.6 k | 84.6 k | 93.1 k | 63.6 k | 33.2 k | 30.2 k |
| port | 84.3 k | 87.4 k | 89.7 k | 88.9 k | 90.8 k | 95.9 k | 55.5 k | 40.3 k | 46.1 k |

The ports are written to be read (loops over the FIR's pairs and the 4 points, the 900's phases through an index
function), 3-21 % over the original, the 900 53 %; the SLP is cheaper (its unused envelope is gone). Speed is for
the improvement step, the bit-exact versions are the reference it is checked against.

In the firmware: `--fw FW.bin [--no-unicorn]` packs the blocks into a library, installs it over USB in the
emulated firmware as waveod Studio does (read back byte for byte) and plays each block's presets (alone at the
defaults, with Gain 90 / Bass 80 / Treble 20, at the end of factory preset 01A) twice: with the original module in
the slot and with the port. Result: all 9 give the same DAC words, every sample, in all three presets. The tone
counts its sine from its own start, and one play of the empty preset comes first: the session's first play differs
from the next ones whatever block plays (the firmware's start-up ramps).

On the pedal (protocol 11 bench, tools/bench.py): every port gives the same CRC as its original module.

## A firmware on the ports alone (`tools/portfw.py`)

`python tools/portfw.py BASE.bin OUT.bin` puts each port, built as a module, into the original's own library
entry (section 1) in place of the original's ELF; the entry's header (id, name, parameters and their defaults,
tab) is kept, so every preset plays on it with the same knobs, and the app loads a block built as a module from
any entry (no change to it). `--keep NAME` leaves a block on the original's module. 174 entries change; the 5
left (FX Loop, SND, RTN, Cab, User IR) never run the original's code with waveod's defaults. In the emulator the presets play the same
DAC words as on the original's modules (first on 2026-11-24: the factory presets 0-2); flashed to the pedal
the same day.
