# Synth (FX › Synth)

The guitar turned into a monophonic synthesizer: the pitch of what is played drives oscillators, a resonant
filter and envelopes. A module like the other own blocks (`src/blocks/synth.c`, the tracker in
`src/blocks/synth/track.h`), in the update file by default (docs/blocks.md).

## Signal flow

1. **Tracking** follows the clean guitar at the chain's input (`io->aux`), not the signal at the block. The Synth
   can sit after a drive or an amp and still follow what is played.
2. **Note logic** turns the tracker's estimates into the note being played.
3. **Voice:** oscillator 1 and oscillator 2, through a 4-pole ladder filter.
4. **Level:** the guitar's own envelope (Follow) or the note's peak held (Hold).
5. **Output:** the synth (mono, on both sides) mixed with the signal at the block.

## The tracker (`synth/track.h`)

**Pre-processing:**
- The input's two channels are averaged.
- They go through a 40 Hz high pass and a 1.2 kHz 4th-order Butterworth low pass.
- One sample in four is kept (11025 Hz), as integers (2^20 = full scale).

**Difference sums.** For every lag t of 1..181 (1575 Hz down to 61 Hz) the tracker keeps the squared-difference
sum

    d(t) = sum over the last W(t) samples of (x[j] - x[j - t])^2,   W(t) = max(2 t, 40)

- **Updated sample by sample:** one term in, one term out, as (a - b)(a + b).
- **Exact:** the sums are 64-bit integers, so there is no drift and the pedal computes what the PC computes.
- **Energy:** the matching energy m(t) = sum of x[j]^2 + x[j - t]^2 comes from a running energy sum.

**Estimate.** Every block (1.45 ms):
- The normalised function n(t) = 1 - d(t) / m(t) is formed. This is McLeod's NSDF: 1 means periodic at t.
- The search starts past the lobe around t = 0 and looks at its local maxima.
- The first maximum that reaches 0.88 of the highest is the period, refined by a parabola.
- A period shorter than 16 samples (a note above ~690 Hz) is measured again over the whole periods that fit in
  32 samples. There the parabola is that many times finer (E6: 4.8 cents off becomes under 3).

**Why the window grows with the lag.** A window two periods long at each lag makes the latency proportional to
the period. A high note needs a short window, a low one a long window. A fixed window would make every note wait
for the low E's.

**Measured.**
- **Synthetic steps:**

  | Note change | Latency |
  |---|---|
  | 330 -> 440 Hz | 6.3 ms |
  | 659 -> 880 Hz | 4.9 ms |
  | 82 -> 110 Hz | 19.4 ms |
  | 110 -> 82 Hz | 26.7 ms |

  The low E's latency is physical: a period of the new note must be seen, plus the window.
- **Steady tones:** E2..E6 within 3 cents (`tests/test_synth.c`).
- **DI recordings (bends, legato, open strings, a fast passage):** these were checked against an offline reference
  where two methods agree (YIN and a harmonic-sum spectrum). Where one of them is an octave off, the frame is left
  out.
  - The raw estimates are on the right semitone in 96.6-99.8 % of the blocks, with 0-0.9 % octave errors.
  - Most of the rest is note changes the tracker has not seen yet. The reference is centred on its window, so it
    sees them earlier.
- **Two strings ringing together** (a low string left open under a high note) are two pitches. A monophonic
  tracker follows one of them and may switch between them.

**Cost.** Per block:
- the sums: 16 samples × 181 lags, one 32 × 32 -> 64-bit multiply-accumulate each;
- the search: 181 divisions, with d and m shifted alike to 30 bits and converted once each.

On the pedal (`tools/bench.py`, 2026-11-27):

| Case | Cycles per block | Share of a block |
|---|---|---|
| On (median) | 66.4 k | 7.6 % |
| On, p99 | 66.8 k | |
| Bypassed | 52.3 k | 6.0 % |
| Cold cache | ~71 k | |

The waves cost differently, with Osc 2 at 100 % (both oscillators running), 2026-11-27:

| Wave | Cycles per block | Share of a block |
|---|---|---|
| Saw | 71.8 k | 8.2 % |
| Sync | 79.5 k | 9.1 % |
| Voice | 81.0 k | 9.3 % |
| Organ | 91.3 k | 10.5 % |
| Supersaw | 91.6 k | 10.5 % |

Bypassed, the tracker keeps listening. By switching parts off:
- the difference sums take ~35 k;
- the search takes ~20 k;
- the voice takes ~14 k.

The sums are bound by instruction count, not memory: the compiler's unrolled loop spills registers. A second,
half-rate set of sums for the long lags (the low notes) would about halve them. That is not done yet.

State: 18 KB per instance in the state pool, with no buffer.

## Note logic

- **Attacks:** a string picked is found on the input's first difference, which is bright where a pick's attack
  is and the strings left ringing mostly are not. An attack is that signal's peak (30 ms release) at three
  times its 50 ms mean, with the level rising (1.3 times its 30 ms mean) and above 4 × the floor, at most every
  50 ms. The level's rise is what tells a pick from a finger sliding on the frets: the slide is bright too, but its
  level does not rise. On the open-string arpeggio take this found the six strings and nothing else.
- **Starting a note:** from silence a note starts when two estimates in a row agree (within half a semitone),
  with clarity ≥ 0.7 and the level above the **Gate** (-70 dBFS RMS by default). A note playing goes on down to
  half of 8 dB under the Gate (-84 dBFS by default). The
  pedal's input noise is at -90 dBFS, measured over USB recording with nothing played. A tail decaying at -74
  started random high notes with the start at -78.
- **Changing note:** by votes for a candidate.
  - A candidate's own estimates add a vote, or four when the clarity is ≥ 0.9. They add none under clarity 0.7
    outside an attack's window: a middling estimate holds a note but does not start another. The end of a slide
    gets there by following.
  - While a note plays, estimates of the note playing and of any third pitch take one away.
  - **In the 150 ms after an attack:** the clarity needed falls to 0.5, and two agreeing estimates change the
    note (four for an octave jump). Only one change per attack is allowed. Raising either one hardly helped
    against a brushed string, and it cost the arpeggio its notes: an attack's strength (level rise 1.4..2.2,
    brightness 3..9) is the same for a soft pick, a new string under ringing ones and a brush.
  - **Without an attack** (legato, or strings ringing together): the change needs 8 net votes, two very clear
    estimates for a clean legato note.
    - A jump of an octave or more (or an octave and a fifth) needs 32. That is what a wrong period looks like, or
      a string an octave down ringing in sympathy, or a harmonic from a finger brushing a string: 66 -> 88 on the
      pedal.
    - Under the legato floor the note does not change at all, with or without an attack, and such a pitch
      gathers no votes. The legato floor is the Gate or -70 dBFS RMS, the lower: a high Gate is for new notes,
      and at -63 it blocked soft pull-offs at -65..-77. Below it are a tail, the rest of a touched string, and a
      string being stopped. Its votes used to gather anyway and held the next picked note back by up to 110 ms.
    - Nor while the level falls fast (under 0.7 of its ~20 ms mean, -3 dB). That is a string being stopped before
      the next pick: its harmonics read an octave or two up, very clear (+12, +24 on the pedal), and its rest a
      low note. Hammer-ons and pull-offs excite the string again, so the level holds or rises.
    - An attack starts the votes again.
    - On the user's two test takes (VIOLINO knobs, Gate -63): stretches of 10 ms or more with the synth 5+
      semitones off the guitar went 9 -> 4, notes reached 60 ms+ late 9 -> 1, and the right semitone 94.0 ->
      95.3 % (take 2). 93 % of what is left is the change to a new note, under 30 ms.
    - Brushing another string was the fourth report from the hardware. On the USB recordings (VIOLINO knobs), the
      note left and came back within 300 ms 12 -> 4 times in the touch take, and 52 -> 15 and 25 -> 7 in the
      slide takes. On the DI takes the octave errors went 0.6 -> 0.3 %.
  - Strings ringing together pull the estimates back and forth between them. With votes, that never adds up to
    a change.
- **Holding a note:** while a note plays, any estimate with clarity ≥ 0.5 and half the level is a pitch. Such an
  estimate keeps the note from ending. A ringing string's clarity drifts down as it decays, and so does a slide's.
  - Within ±0.6 semitone of the note, the estimate moves the pitch (bends, vibrato, slides) 20 % of the way
    (~6 ms) times a weight. The weight is 1 from clarity 0.7, and ((c - 0.5) / 0.2)^2 under it.
  - The other strings ringing pull an estimate (0.5..0.6) by tens of cents, so with the weight that warble
    barely moves it. A slide's middling estimates still carry the note along.
- **Ending a note:** without a clear pitch the note goes on at its last pitch while the guitar sounds (strings
  ringing together, a pick's attack). It ends 25 ms after the guitar falls under half the floor, or after 1 s
  without a pitch.
- **Retrigger:** a note picked (an attack: a new note or the same one again) or a note from silence sets the
  filter's envelope again. In Hold it also takes the level held again.
  - With Attack above 0, a pick while a note sounds also attacks the note again, like a new stroke of a bow:
    the gate falls 48 dB in 2.9 ms (halved at each of 8 control steps, moving linearly over each step's frames
    so it does not click) and rises again with Attack.
  - Legato and slides stay under one stroke. An envelope retrigger like this, with a short Attack (around
    60 ms), is what makes a bowed sound convincing.
  - On the touch take with the VIOLINO knobs, the synth's level jumped up 12 dB or more within 5 ms 5 times
    before, 0 now. The largest sample step stayed the same (no clicks).
  - A change without an attack (a slide, legato) keeps both.
  - On the pedal, taking the string's level again at the end of every slide dropped the synth by 15-20 dB in Hold,
    which sounded like a mute. That was the third report from the hardware.

**Slides on the pedal (Hold mode, USB recordings of the dry input, 2026-11-27).** Drops of the synth of 8 dB or
more within 100 ms while the guitar sounds:

| Take | Before | Now |
|---|---|---|
| 96 s of slides | 22 | 2 |
| 51 s of slides | 9 | 0 |

The two left are a pick and a note from silence.

**From the hardware (2026-11-27).** The first version ended a note after 25 ms without a clear pitch, and took a
new note on two agreeing estimates at any time. On the pedal:
- the sound cut whenever notes rang into each other;
- an open-string arpeggio let ring lost its notes after the second string. Each new string came late, or was
  taken over by the strings still ringing, and the synth jumped between them.

Measured on the DI takes, the first version against now:

| Measure | First version | Now |
|---|---|---|
| Synth silent while the guitar sounds | 4.0 % | 0.5 % |
| Note changes over the four takes | ~350 | 206 |
| Note accuracy | 96.7 % | 95.7 % |

- The rest of the silent time is the start of each note.
- On the arpeggio, the strings come in at their attacks: E2 at 0.77 s, the attack at 0.74 s.
- The note accuracy is a little lower in the fast passage, where a soft-picked note without a detected attack
  waits for its votes.
## Voice

- **Oscillators:** twelve waves, in the Wave knob's order. A new wave goes at the end, so presets keep theirs.
  - **Saw, Square, Pulse (25 %), Thin Pulse (12.5 %):** PolyBLEP. The pulses have their DC (2 w - 1) taken out:
    under the envelopes a pulse's mean would thump.
  - **Triangle:** plain.
  - **Sine:** polynomial.
  - **PWM:** a pulse whose width swings 0.15..0.85 at 0.8 Hz.
  - **Saw+Square:** the two summed, the square inverted so the odd harmonics add. In phase they cancelled, and
    the sum sounded an octave up.
  - **Supersaw:** the saw plus four side saws at ±6 and ±16 cents (level 0.6, starting at spread phases), scaled
    back to one saw's RMS.
  - **Sync:** a slave saw restarted at each period of the note. It runs at 2 times the note, up to 2^(1 + Env
    Amt's octaves / 2) with the filter's envelope: the sync sweep. The restart's jump is smoothed after it like a
    saw's.
  - **Organ:** drawbars 8', 4', 2 2/3', 2', 1 1/3' (harmonics 1, 2, 3, 4, 6; levels 1, 0.7, 0.5, 0.45, 0.3) by
    angle sums from one sine and cosine. Each harmonic is left out above 0.45 fs.
  - **Voice:** a saw through three TPT band passes, an "aah": F1 800 Hz Q 8, F2 1150 Hz Q 9, F3 2900 Hz Q 12,
    gains 1, 0.63, 0.25.
  - Osc 2 plays the same wave. The two pass a 5 Hz DC blocker, since Sync's restarts leave a DC.
  - Levels against the saw (A3, the filter open): Square +4.7 dB, Pulse +3.4, Triangle +0.1, Sine +1.8, Thin
    Pulse +0.9, PWM +3.3, Saw+Square +2.4, Supersaw -2.2, Sync 0.0, Organ +0.4, Voice +1.1.
  - Oscillator 2 sits Interval semitones plus Detune cents above, at the Osc 2 level.
  - The sub octave is Osc 2 at Interval -12. A Sub knob of its own (a square an octave below) gave its place to
  the Gate.
  - Octave shifts all three.
  - The sum is normalised by 1 + Osc 2.
- **Glide:** moves the pitch from note to note (one-pole, in semitones). From silence a note starts on its pitch.
- **Snap:** rounds the pitch to the semitone (A = 440 Hz).
- **Filter:** a 4-pole ladder, zero-delay feedback (TPT), solved per sample.
  - Resonance 0..100 % is a feedback of 0..3.9, with half of its passband loss given back.
  - The input is soft-clipped, x / (1 + 0.3 |x|).
  - The cutoff follows the note: Cutoff 0..100 % is 1..128 times the note's frequency, plus Env Amt (0..6
    octaves) of the envelope.
  - The envelope jumps to 1 at a retrigger and falls with Decay. The coefficients move linearly over each
    16-frame step.
- **Level:**
  - **Follow:** the guitar's envelope, a peak follower (150 ms release) smoothed over 4 ms, times a gate. The gate
    rises with Attack and closes in 30 ms when the note ends.
  - **Hold:** the note's peak, held until the note ends, then a release of Decay.
  - The synth is scaled by 0.5: at the guitar's peak level, a saw is about that much louder.
- **Mix:** 0 = the signal at the block, 50 = both at full level, 100 = the synth (as the FX Loop's mix). Level
  then applies to the synth.
- **Bypassed:** the synth fades out over a block and the signal passes bit for bit. The tracker keeps listening,
  so switching it on starts on the note being played.

## Knobs

| Knob | Range | Default |
|---|---|---|
| Wave | Saw / Square / Pulse / Triangle / Sine / Thin Pulse / PWM / Saw+Square / Supersaw / Sync / Organ / Voice | Saw |
| Octave | -2..+2 | 0 |
| Osc 2 | 0..100 % | 0 |
| Interval | -24..+24 semitones | +7 |
| Detune | 0..50 cents | 8 |
| Gate | -90..-40 dB (RMS; a value above, from a preset made before the Gate, is the default) | -70 |
| Glide | 0..500 ms | 0 |
| Cutoff | 0..100 % (1..128 × the note) | 40 |
| Resonance | 0..100 % | 30 |
| Env Amt | 0..100 % (0..6 octaves) | 50 |
| Attack | 0..1000 ms | 0 |
| Decay | 10..2000 ms | 300 |
| Mode | Follow / Follow Snap / Hold / Hold Snap | Follow |
| Mix | 0..100 % | 100 |
| Level | -20..+20 dB | 0 |

Fifteen knobs is the SDK's limit (`WVFX_MAX_PARAMS`, and the presets' 15 knob words), which is why Snap
shares the Mode switch and the Gate took Sub's place. Raising the limit for every block is planned separately. Cutoff can be
moved by an expression pedal or a modulator like any knob (docs/controls.md), which makes a synth wah.

## Verification

- `tests/test_synth.c`:
  - the descriptor;
  - the tracker on steady tones and on note changes, with the latency bounds above;
  - the block's pitch, measured from a triangle's zero crossings, at Octave +1 and with Snap;
  - silence and -80 dB noise giving exact silence;
  - bypass, bit for bit, from the start and after the fade;
  - every knob moving every block: finite and bounded.
- `tools/check_ownfw.py`: the module in the update file, listed once in FX, plays in the emulator.
- The DI measurements above were made outside this repository with a host harness that runs the block on WAV
  files.
