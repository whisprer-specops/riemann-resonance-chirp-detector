# Changelog

## 0.2.0 - SDR++ host milestone

- Added `sdrpp_modules/riemann_chirp_overlay`, a native C++17 SDR++ module scaffold.
- Added C++ detector core mirroring the Python detector design:
  - rolling FFT waterfall history,
  - robust per-bin background normalization,
  - slope/ridge scanning,
  - Rényi sharpness scoring,
  - permutation-entropy structure scoring,
  - entropy-centrality-inspired scoring,
  - optional zeta-zero log-periodic resonance feature.
- Added SDR++ module UI controls for detector thresholds, feature weights, slope range, and overlay drawing.
- Added standalone native detector smoke test independent of SDR++ headers.
- Added SDR++ root CMake snippet and integration instructions.
