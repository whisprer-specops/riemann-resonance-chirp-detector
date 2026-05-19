from __future__ import annotations

import itertools
import math
from typing import Dict, Tuple

import numpy as np


def _safe_probabilities(values: np.ndarray) -> np.ndarray:
    x = np.asarray(values, dtype=np.float64).reshape(-1)
    x = x[np.isfinite(x)]
    if x.size == 0:
        return np.array([1.0], dtype=np.float64)
    x = x - np.min(x)
    total = float(np.sum(x))
    if total <= 1e-15:
        return np.full(x.size, 1.0 / x.size, dtype=np.float64)
    p = x / total
    return p[p > 0]


def shannon_entropy(values: np.ndarray, normalized: bool = True) -> float:
    p = _safe_probabilities(values)
    h = float(-np.sum(p * np.log(p)))
    if normalized:
        denom = math.log(max(p.size, 2))
        h /= denom
    return float(np.clip(h, 0.0, 1.0 if normalized else np.inf))


def renyi_entropy(values: np.ndarray, alpha: float = 2.0, normalized: bool = True) -> float:
    if alpha <= 0 or abs(alpha - 1.0) < 1e-12:
        raise ValueError("alpha must be > 0 and != 1")
    p = _safe_probabilities(values)
    h = float(np.log(np.sum(np.power(p, alpha))) / (1.0 - alpha))
    if normalized:
        denom = math.log(max(p.size, 2))
        h /= denom
    return float(np.clip(h, 0.0, 1.0 if normalized else np.inf))


def renyi_sharpness(values: np.ndarray, alpha: float = 2.0) -> float:
    """Convert normalized Rényi entropy into concentration/sharpness score."""
    return float(np.clip(1.0 - renyi_entropy(values, alpha=alpha, normalized=True), 0.0, 1.0))


def permutation_entropy(series: np.ndarray, order: int = 4, delay: int = 1, normalized: bool = True) -> float:
    """Bandt-Pompe permutation entropy for a 1D series.

    This uses ordinal patterns, making it robust against absolute gain changes in
    SDR frontends and waterfall color scaling.
    """
    x = np.asarray(series, dtype=np.float64).reshape(-1)
    x = x[np.isfinite(x)]
    if order < 2:
        raise ValueError("order must be >= 2")
    if delay < 1:
        raise ValueError("delay must be >= 1")
    needed = (order - 1) * delay + 1
    if x.size < needed + 1:
        return 1.0 if normalized else 0.0

    counts: Dict[Tuple[int, ...], int] = {}
    for start in range(0, x.size - needed + 1):
        window = x[start : start + needed : delay]
        # Stable tie handling: lexsort by value then index.
        pattern = tuple(np.lexsort((np.arange(order), window)).tolist())
        counts[pattern] = counts.get(pattern, 0) + 1
    probs = np.array(list(counts.values()), dtype=np.float64)
    probs /= probs.sum()
    h = float(-np.sum(probs * np.log(probs)))
    if normalized:
        h /= math.log(math.factorial(order))
    return float(np.clip(h, 0.0, 1.0 if normalized else np.inf))


def permutation_structure_score(series: np.ndarray, order: int = 4, delay: int = 1) -> float:
    """Return low-entropy ordinal structure as a positive detection feature."""
    return float(np.clip(1.0 - permutation_entropy(series, order=order, delay=delay, normalized=True), 0.0, 1.0))


def entropy_decay_centrality(frame: np.ndarray, freq_indices: np.ndarray) -> float:
    """Entropy-decay-inspired centrality for candidate occupied frequency bins.

    Treat the frequency-axis energy distribution as a graph-free structural prior:
    removing bins occupied by a candidate path should reduce entropy more when the
    candidate passes through informationally important parts of the band.
    """
    arr = np.asarray(frame, dtype=np.float64)
    if arr.ndim != 2 or arr.shape[1] < 2:
        return 0.0
    energy = np.maximum(arr - np.nanmin(arr), 0.0)
    per_freq = energy.sum(axis=0)
    p = _safe_probabilities(per_freq)
    if p.size != arr.shape[1]:
        # Degenerate all-flat frame.
        return 0.0
    full_h = shannon_entropy(per_freq, normalized=False)
    idx = np.asarray(freq_indices, dtype=np.int64).reshape(-1)
    idx = idx[(idx >= 0) & (idx < arr.shape[1])]
    if idx.size == 0:
        return 0.0
    reduced = per_freq.copy()
    unique = np.unique(idx)
    reduced[unique] = 0.0
    reduced_h = shannon_entropy(reduced, normalized=False)
    max_h = math.log(arr.shape[1])
    if max_h <= 1e-12:
        return 0.0
    decay = max(full_h - reduced_h, 0.0) / max_h
    # A path spans only a subset of bins; scale gently by coverage.
    coverage = min(unique.size / max(arr.shape[1], 1), 1.0)
    return float(np.clip(decay / max(coverage, 1e-3), 0.0, 1.0))
