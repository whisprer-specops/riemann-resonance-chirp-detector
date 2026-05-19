from __future__ import annotations

from dataclasses import dataclass
from typing import Tuple

import numpy as np


@dataclass(frozen=True)
class SyntheticWaterfallSpec:
    time_bins: int = 256
    freq_bins: int = 512
    noise_sigma: float = 0.17
    seed: int = 1337


def make_synthetic_waterfall(spec: SyntheticWaterfallSpec = SyntheticWaterfallSpec()) -> tuple[np.ndarray, list[dict]]:
    """Create a synthetic waterfall containing chirps and distractors."""
    rng = np.random.default_rng(spec.seed)
    frame = rng.normal(loc=0.0, scale=spec.noise_sigma, size=(spec.time_bins, spec.freq_bins))
    frame += rng.exponential(scale=0.025, size=frame.shape)

    truth: list[dict] = []
    chirps = [
        (int(0.10 * spec.time_bins), int(0.47 * spec.time_bins), 0.16 * spec.freq_bins, 0.72, 1.35),
        (int(0.45 * spec.time_bins), int(0.90 * spec.time_bins), 0.82 * spec.freq_bins, -0.86, 1.10),
        (int(0.27 * spec.time_bins), int(0.72 * spec.time_bins), 0.55 * spec.freq_bins, 0.24, 0.82),
    ]
    for t0, t1, f0, slope, amp in chirps:
        _draw_chirp(frame, t0, t1, f0, slope, amp, width=2.2, rng=rng)
        truth.append({"t0": t0, "t1": t1, "f0": f0, "slope": slope})

    # Narrowband and burst distractors.
    for f in (80, 340):
        frame[:, max(0, f - 1) : min(spec.freq_bins, f + 2)] += 0.20
    for _ in range(12):
        t = int(rng.integers(0, spec.time_bins))
        f = int(rng.integers(0, spec.freq_bins))
        frame[max(0, t - 2) : min(spec.time_bins, t + 3), max(0, f - 4) : min(spec.freq_bins, f + 5)] += rng.uniform(0.4, 0.9)

    return frame.astype(np.float32), truth


def _draw_chirp(frame: np.ndarray, t0: int, t1: int, f0: float, slope: float, amp: float, width: float, rng: np.random.Generator) -> None:
    time_bins, freq_bins = frame.shape
    for t in range(max(0, t0), min(time_bins, t1)):
        phase_mod = 0.18 * np.cos(14.134725141734693 * np.log1p((t - t0 + 1) / 8.0))
        center = f0 + slope * (t - t0) + phase_mod
        lo = max(0, int(center - 5 * width))
        hi = min(freq_bins, int(center + 5 * width + 1))
        xs = np.arange(lo, hi, dtype=np.float64)
        profile = amp * np.exp(-0.5 * ((xs - center) / width) ** 2)
        flutter = 0.80 + 0.20 * rng.random()
        frame[t, lo:hi] += profile * flutter
