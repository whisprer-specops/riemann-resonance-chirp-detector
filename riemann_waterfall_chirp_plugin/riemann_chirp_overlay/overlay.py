from __future__ import annotations

from typing import Iterable, Tuple

import numpy as np

from .config import RiemannChirpConfig
from .types import ChirpDetection


def build_overlay_rgba(shape: Tuple[int, int], detections: Iterable[ChirpDetection], config: RiemannChirpConfig) -> np.ndarray:
    """Create an RGBA overlay image for a waterfall frame.

    Returned array has shape ``(time, frequency, 4)`` and dtype ``uint8``.
    It intentionally does not assume a host color map; the host can alpha-blend.
    """
    time_bins, freq_bins = shape
    rgba = np.zeros((time_bins, freq_bins, 4), dtype=np.uint8)
    alpha = int(round(np.clip(config.overlay_alpha, 0.0, 1.0) * 255))

    for det in detections:
        # Score-scaled yellow/orange/red ramp without relying on matplotlib.
        s = float(np.clip(det.score, 0.0, 1.0))
        red = 255
        green = int(round(90 + 140 * s))
        blue = int(round(20 + 45 * (1.0 - s)))
        color = np.array([red, green, blue, alpha], dtype=np.uint8)
        t0, f0, t1, f1 = det.bbox
        t0 = max(0, min(time_bins - 1, t0))
        t1 = max(t0 + 1, min(time_bins, t1))
        f0 = max(0, min(freq_bins - 1, f0))
        f1 = max(f0 + 1, min(freq_bins, f1))

        if config.draw_boxes:
            rgba[t0:t1, f0 : min(f0 + 2, f1)] = color
            rgba[t0:t1, max(f0, f1 - 2) : f1] = color
            rgba[t0 : min(t0 + 2, t1), f0:f1] = color
            rgba[max(t0, t1 - 2) : t1, f0:f1] = color

        if config.draw_ridge:
            local_t = np.arange(t1 - t0, dtype=np.float64)
            freqs = np.rint(det.intercept_freq_bin + det.slope_bins_per_row * local_t).astype(int)
            for i, f in enumerate(freqs):
                tt = t0 + i
                if 0 <= tt < time_bins:
                    for df in range(-max(config.ridge_half_width_bins, 1), max(config.ridge_half_width_bins, 1) + 1):
                        ff = f + df
                        if 0 <= ff < freq_bins:
                            rgba[tt, ff] = color
    return rgba


def alpha_blend_grayscale(frame: np.ndarray, overlay_rgba: np.ndarray) -> np.ndarray:
    """Return an RGB preview blending a normalized grayscale frame with overlay."""
    arr = np.asarray(frame, dtype=np.float64)
    arr = arr - np.nanmin(arr)
    denom = np.nanmax(arr) if np.nanmax(arr) > 1e-12 else 1.0
    base = np.clip(arr / denom, 0.0, 1.0)
    rgb = np.repeat((base * 255).astype(np.uint8)[..., None], 3, axis=2).astype(np.float64)
    overlay = overlay_rgba[..., :3].astype(np.float64)
    alpha = overlay_rgba[..., 3:4].astype(np.float64) / 255.0
    blended = rgb * (1.0 - alpha) + overlay * alpha
    return np.clip(blended, 0, 255).astype(np.uint8)
