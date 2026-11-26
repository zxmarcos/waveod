# A .nam converted for the original's Clone

waveod Studio's NAM screen can store any `.nam` in a Clone slot as a capture of the original's own format, the one
the original's PC editor makes, which the Clone block plays (`blocks/clone/clone.c`, the original's ToneClone bit for
bit; docs/stock_port.md "The Clone"). The editor's converter only takes the WaveNet A1 files its old bundled
NeuralAmpModelerCore loads; this one runs the current core, so WaveNet A1 of any size, A2 (SlimmableContainer),
LSTM, ConvNet and Linear models at any sample rate all convert. The capture is an approximation of the model (a
filter, a saturation, a filter) and much lighter than the NAM engine: about 125 000 cycles a block on the pedal.

## How

`tools/clonefit/clonefit.cpp` (built by `python tools/wvbuild.py clonefit` into `out/clonefit/clonefit.exe`, with
NeuralAmpModelerCore, Eigen and nlohmann/json from `third_party/nam`) does what the editor's HTUSBTools library does
(`NamConvertThread::run`, `getNamOutput`, `HTKPA::startClone` and its helpers, read from the macOS build, which keeps
the names):

1. A 70 s reference DI at 44.1 kHz, resampled to the model's rate, goes through the model; its output x 0.31 is kept
   at 16 bits, as the editor writes it to a WAV.
2. The Clone's model is fitted to the DI / output pair at the model's rate:
   - IIR 1 is the identity and IIR 2 a 40 Hz Butterworth high pass (the editor fits neither);
   - the saturation `A (1 - e^(-k x))` per polarity: A from the output's peaks over the first 5 s (a rising sine),
     k from the small-signal slope, then scaled by the best of 0.8 .. 1.2 for each side;
   - two transfer functions (Welch, Hamming 0.125 s, nfft 2048) of the model without IRs against the output, one
     on quiet noise, one on a loud sweep through a cabinet-like FIR; their ratio starts IR 1 (128 taps, before the
     saturation) and IR 2 (2048 taps, after it);
   - `iterAmpCoeff` three times (the sweep 3 passes, the quiet noise 2, the rising pink noise 5) moves spectral
     shape between IR 1 and IR 2 so the whole chain follows the output (its error: the mean |ln| of the residual
     transfer at 512 mel-spaced frequencies, 80 Hz - 10 kHz); IRs come from minimum-phase reconstructions;
   - a last correction of IR 2 from the averaged spectra of the model and the output over the music (50 - 70 s),
     then its energy matched and x 4 (the pedal's level).
3. The IRs are resampled to 44.1 kHz and the capture written: "VTSI", 0x2288 bytes, CRC-16 (Modbus) big-endian at 8;
   a Clone slot keeps its first 0x2000 bytes (the original's file system does the same).

### The reference DI

The editor's own DI (`nam_input_wav.wav`, installed with it) is used when the Studio finds it (Program Files, or
`WAVEOD_CLONE_DI`); else the converter's own (`clonefit ... builtin`), made with the same sections: a 300 Hz sine
rising from -60 dBFS (30 dB/s for 1 s, then 7.5 dB/s) to full scale, a click at 6 s and white noise at -62 dBFS
to 21 s, an exponential sweep 20 Hz - 10 kHz at -5 dBFS from 23 s, pink noise from 30 s rising from -28.5 to
-24.7 dBFS rms (a 1 s loop with a crest factor of 3, as the editor's), and from 50 s guitar phrases
(Karplus-Strong). The pink noise and the guitar take the editor's long-term 1/3-octave shape (its band levels
only; no sample of it is in waveod).

## Checked

- The fit against the editor's own (its library called through `namConvertCloData`, an A1 model, the same DI): the
  saturation parameters within 0.05 %, the IRs' spectra within 0.1 dB on average (0.4 dB at most, 60 Hz - 12 kHz),
  and the Clone block playing either capture: 1/3-octave levels within 0.1 - 0.3 dB on average. The rest is the
  resamplers' and float sums' order (the editor's resampler is r8brain).
- Against the model (the Clone block's output vs the NAM output, 1/3-octave levels, the mean offset removed): about
  1.2 - 1.7 dB on average for the editor's capture and for this one with the editor's DI; with the built-in DI 1.5 -
  2 dB on signals neither fit used (pink and white noise at other levels, guitar).
- In the emulator: the capture written over the protocol (CLONE_DATA, as the editor does) reads back as "captura do
  Clone original".

## Use

In waveod Studio, NAM screen: pick a slot, then a `.nam`; a file without the A2-Lite network converts (with one, the
sheet offers the NAM engine or the conversion). The conversion takes 10 s to 1.5 min (the model's size) on the PC;
then the slot is written. From the command line:

    python tools/wvbuild.py clonefit
    out/clonefit/clonefit.exe MODEL.nam builtin|DI.wav OUT.clo [--target-out T.wav] [--di-out D.wav] [--slim S]
