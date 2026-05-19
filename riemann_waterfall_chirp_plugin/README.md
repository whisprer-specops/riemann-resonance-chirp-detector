# Riemann Chirp Overlay

A software-agnostic Python plugin module for SDR/waterfall display software. It highlights likely chirp / LFM / swept-frequency communication signals using a practical fusion of:

- slope/ridge scanning over waterfall frames,
- Rényi entropy sharpness,
- permutation entropy structure scoring,
- zeta-zero-weighted “Riemann resonance” coherence,
- entropy-decay-inspired frequency centrality.

The module does **not** demodulate, decrypt, exploit, or decode communications. It only produces visual overlays and detection metadata for signal-analysis displays.

## What this is

Most waterfall applications already have a way to obtain either:

1. a 2D waterfall/spectrogram frame as a numeric array, or
2. IQ samples that are converted to a waterfall internally.

This package targets the first case. Give it a 2D NumPy array shaped `(time_bins, frequency_bins)` and it returns:

- a list of chirp detections,
- an RGBA overlay mask,
- an optional heatmap layer.

The same core can then be wrapped into GNU Radio, SDR++, SDR#, Gqrx/CubicSDR sidecar scripts, a Qt overlay, or a networked display pipeline.

## Install

From the project folder:

```bash
python -m pip install -e .[demo,test]
```

For runtime only:

```bash
python -m pip install -e .
```

## Quick demo

Generate a synthetic waterfall, detect chirps, and save overlay outputs:

```bash
rrchirp demo --out-dir demo_out
```

Outputs:

- `demo_out/waterfall.npy`
- `demo_out/detections.json`
- `demo_out/overlay.png`
- `demo_out/heatmap.npy`

## Scan an existing waterfall array

```bash
rrchirp scan waterfall.npy --detections detections.json --overlay overlay.png
```

The `.npy` file must contain a 2D array in `(time, frequency)` order. Values may be linear power, magnitude, or dB-like waterfall intensities; the detector robust-normalizes internally.

## Minimal integration

```python
import numpy as np
from riemann_chirp_overlay import RiemannChirpConfig, RiemannResonanceWaterfallPlugin

plugin = RiemannResonanceWaterfallPlugin(RiemannChirpConfig())
waterfall = np.load("waterfall.npy")  # shape: (time_bins, freq_bins)
result = plugin.process_waterfall_frame(waterfall)

for det in result.detections:
    print(det.label, det.score, det.bbox)

rgba_overlay = result.overlay_rgba
```

## Software adapter contract

A host waterfall display should call:

```python
result = plugin.process_waterfall_frame(frame_2d)
```

Then blend `result.overlay_rgba` onto its waterfall image. Detection bounding boxes are reported as:

```text
(time_start, freq_start, time_end, freq_end)
```

All coordinates are integer bin coordinates in the provided frame.

## Why this design

The project intentionally separates detector core from GUI/plugin host. Waterfall programs differ wildly in plugin ABI and UI renderer internals, but most can pass a frame through a Python sidecar, local socket bridge, or native adapter. This package gives a stable core first.

## Notes on the “Riemann resonance” feature

This implementation does not claim that physical RF chirps are literally governed by the Riemann zeta function. Instead, it uses the imaginary parts of the first non-trivial zeta zeros as a deterministic, nonuniform multiscale log-periodic feature bank. Candidate chirp ridge intensity sequences are scored for coherence against that bank. In practice this is a structured feature-engineering layer that can be enabled, disabled, or reweighted.

## Tests

```bash
pytest -q
```

## Suggested host integrations

- **GNU Radio**: call the core from an Embedded Python block that receives spectrogram tiles.
- **SDR++ / SDR#**: expose waterfall tiles to a sidecar process, then blend returned RGBA overlays in the UI layer.
- **PyQt/PySide waterfall viewers**: pass the displayed NumPy waterfall buffer directly and composite the returned overlay.
- **Web dashboards**: POST a frame to a local service wrapper around `RiemannResonanceWaterfallPlugin` and draw returned detections in canvas/SVG/WebGL.

## SDR++ native module

This package now includes a first native SDR++ module target:

```text
sdrpp_modules/riemann_chirp_overlay/
```

The SDR++ module is C++17 and mirrors the Python detector's core idea directly in native code so SDR++ does not need to embed Python. It samples recent FFT rows from the SDR++ waterfall, builds a rolling waterfall history, detects chirp-like ridges, and draws overlay boxes/ridge lines in the waterfall region.

To validate the native detector core without SDR++ headers:

```bash
cd sdrpp_modules/riemann_chirp_overlay
./tools/build_smoke_detector.sh
```

To integrate it into SDR++, copy `sdrpp_modules/riemann_chirp_overlay` into `SDRPlusPlus/misc_modules/riemann_chirp_overlay`, then apply the snippet in `patches/sdrpp_root_cmake_snippet.txt` to SDR++'s root CMake file and build with:

```bash
cmake .. -DOPT_BUILD_RIEMANN_CHIRP_OVERLAY=ON
```

See `sdrpp_modules/riemann_chirp_overlay/README.md` for full Linux and Windows instructions.
