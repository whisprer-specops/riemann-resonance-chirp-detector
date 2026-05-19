# SDR++ Native Module: `riemann_chirp_overlay`

Native SDR++ module scaffold for highlighting likely chirp / LFM / swept-frequency signals directly on the SDR++ waterfall.

This module is intentionally visual and analytic only. It does not demodulate, decode, decrypt, exploit, classify people, or identify operators. Use it for lawful spectrum monitoring, lab testing, receiver diagnostics, and your own transmissions.

## What it does

The module hooks SDR++'s waterfall redraw path, samples the latest FFT row with `gui::waterfall.acquireLatestFFT(...)`, maintains a rolling waterfall history, runs a native C++ detector, and draws overlay boxes/ridge lines back into the waterfall region.

Feature fusion:

- ridge / slope scanning for swept-frequency traces,
- robust local background normalization,
- Rényi sharpness scoring,
- permutation-entropy structure scoring,
- entropy-centrality-inspired ridge importance,
- optional zeta-zero log-periodic resonance scoring.

## Files

```text
riemann_chirp_overlay/
├── CMakeLists.txt
├── README.md
├── patches/
│   └── sdrpp_root_cmake_snippet.txt
├── src/
│   ├── detector.cpp
│   ├── detector.h
│   └── main.cpp
└── tools/
    ├── build_smoke_detector.sh
    └── smoke_detector.cpp
```

## Standalone detector smoke test

This validates the native detector core without SDR++ headers:

```bash
cd misc_modules/riemann_chirp_overlay
./tools/build_smoke_detector.sh
```

Expected output includes at least one detection:

```text
detections=...
riemann_resonant_chirp score=...
```

## Integrating into SDR++ source

From your SDRPlusPlus source checkout:

```bash
cp -r /path/to/riemann_chirp_overlay ./misc_modules/riemann_chirp_overlay
```

Patch the SDR++ root `CMakeLists.txt` using:

```text
patches/sdrpp_root_cmake_snippet.txt
```

The snippet is:

```cmake
option(OPT_BUILD_RIEMANN_CHIRP_OVERLAY "Build Riemann chirp overlay module" OFF)

if (OPT_BUILD_RIEMANN_CHIRP_OVERLAY)
    add_subdirectory("misc_modules/riemann_chirp_overlay")
endif()
```

Then configure SDR++ with the module enabled.

Linux / BSD example:

```bash
mkdir -p build
cd build
cmake .. -DOPT_BUILD_RIEMANN_CHIRP_OVERLAY=ON
make -j"$(nproc)"
```

Windows example from a Developer PowerShell, adjusted for your vcpkg path:

```powershell
mkdir build
cd build
cmake .. `
  "-DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -G "Visual Studio 17 2022" `
  -DOPT_BUILD_RIEMANN_CHIRP_OVERLAY=ON
cmake --build . --config Release
```

## Loading during SDR++ development

Add the built library to the `modules` list in your SDR++ root config.

Linux development example:

```json
"modules": [
  "./build/radio/radio.so",
  "./build/recorder/recorder.so",
  "./build/misc_modules/riemann_chirp_overlay/riemann_chirp_overlay.so"
]
```

Windows development example:

```json
"modules": [
  "./build/radio/Release/radio.dll",
  "./build/recorder/Release/recorder.dll",
  "./build/misc_modules/riemann_chirp_overlay/Release/riemann_chirp_overlay.dll"
]
```

Start SDR++ with your dev root and console enabled so module loader errors are visible.

Linux:

```bash
./build/sdrpp -r root_dev
```

Windows:

```powershell
./build/Release/sdrpp.exe -r root_dev -c
```

## SDR++ UI controls

After creating/enabling the module in Module Manager, the left panel gets a `riemann_chirp_overlay` section with:

- enable/disable,
- draw boxes,
- draw ridge lines,
- waterfall newest-row orientation toggle,
- detector history/window size,
- slope range,
- minimum score,
- feature weights.

If overlay boxes appear vertically mirrored, toggle **Newest waterfall row at top**.

## Current limitation

This first native host module overlays detections using recent FFT rows collected from SDR++'s public waterfall/FFT API. It does not patch SDR++'s internal waterfall framebuffer, and it does not yet export detections over a socket/API. Those are the next natural milestones once this compiles cleanly against your SDR++ checkout.
