# NeuralAmpModelerCore (for tools/clonefit)

The PC side of waveod Studio's NAM -> Clone conversion runs `.nam` models with the official core
(docs/clone_fit.md). Copied unchanged from https://github.com/sdatkinson/NeuralAmpModelerCore at commit
`0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842` (2026-09-18, core 0.6.0: WaveNet A1 / A2, SlimmableContainer,
LSTM, ConvNet, Linear):

- `NAM/`: the core (MIT, `LICENSE`);
- `Dependencies/nlohmann/json.hpp`: JSON for Modern C++ (MIT);
- `Dependencies/eigen/`: the headers of Eigen the core and clonefit use (`Eigen/`, `unsupported/Eigen/FFT`;
  MPL2 and the others in its `COPYING.*` files).

Only tools/clonefit builds it, for the PC; nothing of it goes into the firmware.
