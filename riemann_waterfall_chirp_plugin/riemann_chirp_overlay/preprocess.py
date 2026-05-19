from __future__ import annotations

import numpy as np


def validate_frame(frame: np.ndarray) -> np.ndarray:
    arr = np.asarray(frame, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError(f"waterfall frame must be 2D, got shape {arr.shape}")
    if arr.shape[0] < 12 or arr.shape[1] < 12:
        raise ValueError(f"waterfall frame too small, got shape {arr.shape}")
    if not np.isfinite(arr).any():
        raise ValueError("waterfall frame contains no finite values")
    arr = np.nan_to_num(arr, nan=float(np.nanmedian(arr[np.isfinite(arr)])), posinf=0.0, neginf=0.0)
    return arr


def robust_normalize(frame: np.ndarray, highpass_rows: int = 5, clip_sigma: float = 7.5) -> np.ndarray:
    """Robustly normalize waterfall intensities for detector scoring."""
    arr = validate_frame(frame)
    x = arr.copy()

    # Remove slow time-axis drift by subtracting a simple moving median-like mean.
    if highpass_rows > 1:
        radius = max(int(highpass_rows) // 2, 1)
        padded = np.pad(x, ((radius, radius), (0, 0)), mode="edge")
        smooth = np.zeros_like(x)
        for i in range(x.shape[0]):
            smooth[i] = np.mean(padded[i : i + 2 * radius + 1], axis=0)
        x = x - smooth

    med = float(np.median(x))
    mad = float(np.median(np.abs(x - med)))
    sigma = 1.4826 * mad if mad > 1e-12 else float(np.std(x) + 1e-12)
    z = (x - med) / sigma
    z = np.clip(z, -clip_sigma, clip_sigma)

    # Shift to positive-ish range for entropy features while preserving contrast.
    z = z - np.percentile(z, 5)
    scale = np.percentile(z, 99) - np.percentile(z, 5)
    if scale <= 1e-12:
        return np.zeros_like(z)
    return np.clip(z / scale, 0.0, 1.0)
