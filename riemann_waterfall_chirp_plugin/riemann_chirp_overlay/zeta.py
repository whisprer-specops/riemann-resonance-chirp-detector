from __future__ import annotations

import functools
from typing import Iterable

import numpy as np

# First imaginary parts of non-trivial Riemann zeta zeros on the critical line.
# These are used as a deterministic, nonuniform log-periodic feature bank.
FIRST_ZETA_ZERO_GAMMAS = np.array(
    [
        14.134725141734693,
        21.022039638771554,
        25.010857580145688,
        30.424876125859513,
        32.935061587739190,
        37.586178158825671,
        40.918719012147495,
        43.327073280914999,
        48.005150881167159,
        49.773832477672302,
        52.970321477714460,
        56.446247697063394,
        59.347044002602353,
        60.831778524609809,
        65.112544048081607,
        67.079810529494173,
        69.546401711173979,
        72.067157674481907,
        75.704690699083933,
        77.144840068874805,
        79.337375020249367,
        82.910380854086030,
        84.735492980517050,
        87.425274613125229,
        88.809111207634465,
        92.491899270558485,
        94.651344040519886,
        95.870634228245309,
        98.831194218193692,
        101.31785100573138,
        103.72553804047834,
        105.44662305232609,
    ],
    dtype=np.float64,
)


def get_zeta_zeros(count: int) -> np.ndarray:
    if count < 1:
        raise ValueError("count must be >= 1")
    if count <= len(FIRST_ZETA_ZERO_GAMMAS):
        return FIRST_ZETA_ZERO_GAMMAS[:count].copy()
    # For counts beyond the hardcoded reliable list, extend with the asymptotic
    # density inverse using a simple monotonic extrapolation. The first 32 values
    # are enough for the default detector; this path preserves API robustness.
    values = list(FIRST_ZETA_ZERO_GAMMAS)
    n = len(values) + 1
    while len(values) < count:
        # Riemann-von Mangoldt rough inverse: gamma_n ≈ 2π n / log(n / 2π).
        denom = max(np.log(max(n / (2.0 * np.pi), 1.2)), 0.25)
        candidate = float((2.0 * np.pi * n) / denom)
        values.append(max(candidate, values[-1] + 1.0))
        n += 1
    return np.array(values, dtype=np.float64)


@functools.lru_cache(maxsize=128)
def zeta_feature_bank(length: int, zero_count: int, scales_tuple: tuple[float, ...]) -> np.ndarray:
    """Return normalized log-periodic templates shaped ``(templates, length)``."""
    if length < 4:
        raise ValueError("length must be >= 4")
    gammas = get_zeta_zeros(zero_count)
    n = np.arange(1, length + 1, dtype=np.float64)
    templates = []
    for scale in scales_tuple:
        if scale <= 0:
            continue
        x = np.log1p(scale * n)
        for gamma in gammas:
            phase = gamma * x
            templates.append(np.cos(phase))
            templates.append(np.sin(phase))
    if not templates:
        raise ValueError("at least one positive zeta scale is required")
    bank = np.asarray(templates, dtype=np.float64)
    bank -= bank.mean(axis=1, keepdims=True)
    norm = np.linalg.norm(bank, axis=1, keepdims=True)
    bank = bank / np.maximum(norm, 1e-12)
    return bank


def zeta_resonance_score(series: np.ndarray, zero_count: int, scales: Iterable[float]) -> float:
    """Score ridge intensity coherence against the zeta-zero feature bank.

    The return value is in ``[0, 1]``. It is deliberately conservative: random
    noise tends toward low values, while structured log-periodic modulation along
    a candidate chirp ridge pushes the score upward.
    """
    x = np.asarray(series, dtype=np.float64).reshape(-1)
    if x.size < 8 or not np.isfinite(x).all():
        return 0.0
    x = x - np.median(x)
    # Remove linear trend so raw chirp brightness gradients do not dominate.
    t = np.linspace(-1.0, 1.0, x.size)
    coeff = np.polyfit(t, x, deg=1)
    x = x - np.polyval(coeff, t)
    norm = float(np.linalg.norm(x))
    if norm <= 1e-12:
        return 0.0
    x = x / norm
    bank = zeta_feature_bank(x.size, zero_count, tuple(float(s) for s in scales))
    corr = np.abs(bank @ x)
    # Use a robust upper quantile instead of max to avoid one accidental spike.
    raw = float(np.quantile(corr, 0.985))
    return float(np.clip(raw, 0.0, 1.0))
