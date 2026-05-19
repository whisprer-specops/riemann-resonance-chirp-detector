from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence, Tuple

import numpy as np

from .config import RiemannChirpConfig
from .entropy import entropy_decay_centrality, permutation_structure_score, renyi_sharpness
from .preprocess import robust_normalize, validate_frame
from .types import BBox, ChirpDetection
from .zeta import zeta_resonance_score


@dataclass(frozen=True)
class _Candidate:
    score: float
    t0: int
    t1: int
    f0: float
    slope: float
    ridge_snr: float
    renyi_sharpness: float
    permutation_structure: float
    zeta_resonance: float
    entropy_centrality: float


def _weighted_score(values: Sequence[Tuple[float, float]]) -> float:
    weight_sum = sum(max(w, 0.0) for w, _ in values)
    if weight_sum <= 1e-12:
        return 0.0
    return float(np.clip(sum(max(w, 0.0) * np.clip(v, 0.0, 1.0) for w, v in values) / weight_sum, 0.0, 1.0))


def _iou(a: BBox, b: BBox) -> float:
    at0, af0, at1, af1 = a
    bt0, bf0, bt1, bf1 = b
    it0 = max(at0, bt0)
    if0 = max(af0, bf0)
    it1 = min(at1, bt1)
    if1 = min(af1, bf1)
    iw = max(0, it1 - it0)
    ih = max(0, if1 - if0)
    inter = iw * ih
    area_a = max(0, at1 - at0) * max(0, af1 - af0)
    area_b = max(0, bt1 - bt0) * max(0, bf1 - bf0)
    denom = area_a + area_b - inter
    return 0.0 if denom <= 0 else inter / denom


def _nms(detections: List[ChirpDetection], iou_threshold: float, max_count: int) -> List[ChirpDetection]:
    kept: List[ChirpDetection] = []
    for det in sorted(detections, key=lambda d: d.score, reverse=True):
        if all(_iou(det.bbox, old.bbox) <= iou_threshold for old in kept):
            kept.append(det)
        if len(kept) >= max_count:
            break
    return kept


class RiemannChirpDetector:
    """Detect chirp-like ridges in 2D waterfall frames."""

    def __init__(self, config: RiemannChirpConfig | None = None) -> None:
        self.config = config or RiemannChirpConfig()
        self.config.validate()
        self._slopes = self._build_slopes()

    def _build_slopes(self) -> np.ndarray:
        c = self.config
        pos = np.linspace(c.min_abs_slope_bins_per_row, c.max_abs_slope_bins_per_row, c.slope_count_per_direction)
        slopes = np.concatenate((-pos[::-1], pos))
        return slopes.astype(np.float64)

    def detect(self, frame: np.ndarray) -> List[ChirpDetection]:
        raw = validate_frame(frame)
        norm = robust_normalize(raw, self.config.highpass_time_rows, self.config.robust_clip_sigma)
        candidates = self._scan_candidates(norm)
        detections = [self._candidate_to_detection(norm, cand) for cand in candidates]
        detections = [det for det in detections if det.score >= self.config.min_score]
        return _nms(detections, self.config.nms_iou_threshold, self.config.max_detections)

    def heatmap(self, frame: np.ndarray) -> np.ndarray:
        """Return an approximate chirp likelihood heatmap shaped like the input frame."""
        raw = validate_frame(frame)
        norm = robust_normalize(raw, self.config.highpass_time_rows, self.config.robust_clip_sigma)
        heat = np.zeros_like(norm, dtype=np.float64)
        for cand in self._scan_candidates(norm, keep_per_window=8):
            t = np.arange(cand.t1 - cand.t0, dtype=np.float64)
            freqs = np.rint(cand.f0 + cand.slope * t).astype(int)
            for local_t, f in enumerate(freqs):
                global_t = cand.t0 + local_t
                for df in range(-self.config.ridge_half_width_bins, self.config.ridge_half_width_bins + 1):
                    ff = f + df
                    if 0 <= global_t < heat.shape[0] and 0 <= ff < heat.shape[1]:
                        heat[global_t, ff] = max(heat[global_t, ff], cand.score)
        return heat

    def _scan_candidates(self, norm: np.ndarray, keep_per_window: int = 5) -> List[_Candidate]:
        c = self.config
        time_bins, freq_bins = norm.shape
        win = min(c.window_time_bins, time_bins)
        if time_bins <= win:
            starts = [0]
        else:
            starts = list(range(0, time_bins - win + 1, c.window_step_bins))
            if starts[-1] != time_bins - win:
                starts.append(time_bins - win)

        all_candidates: List[_Candidate] = []
        for t0 in starts:
            window = norm[t0 : t0 + win]
            per_window: List[_Candidate] = []
            for slope in self._slopes:
                best = self._best_for_slope(window, t0, slope)
                if best is not None:
                    per_window.append(best)
            per_window.sort(key=lambda cand: cand.score, reverse=True)
            all_candidates.extend(per_window[:keep_per_window])
        all_candidates.sort(key=lambda cand: cand.score, reverse=True)
        return all_candidates[: max(c.max_detections * 8, c.max_detections)]

    def _best_for_slope(self, window: np.ndarray, global_t0: int, slope: float) -> _Candidate | None:
        c = self.config
        win, freq_bins = window.shape
        starts = np.arange(freq_bins, dtype=np.float64)
        accum = np.zeros(freq_bins, dtype=np.float64)
        counts = np.zeros(freq_bins, dtype=np.float64)

        # Ridge score: average normalized energy along candidate lines.
        for ti in range(win):
            positions = starts + slope * ti
            left = np.floor(positions).astype(int)
            frac = positions - left
            valid = (left >= 0) & (left < freq_bins - 1)
            if not np.any(valid):
                continue
            row = window[ti]
            values = np.zeros(freq_bins, dtype=np.float64)
            values[valid] = row[left[valid]] * (1.0 - frac[valid]) + row[left[valid] + 1] * frac[valid]
            accum[valid] += values[valid]
            counts[valid] += 1.0

        valid_starts = counts >= max(8, win * 0.70)
        if not np.any(valid_starts):
            return None
        ridge_mean = np.zeros(freq_bins, dtype=np.float64)
        ridge_mean[valid_starts] = accum[valid_starts] / counts[valid_starts]

        best_idx = int(np.argmax(ridge_mean))
        best_energy = float(ridge_mean[best_idx])
        if best_energy <= 1e-9:
            return None

        ridge_values, ridge_freqs = self._extract_ridge(window, best_idx, slope)
        if ridge_values.size < 8:
            return None

        background = self._background_for_candidate(window, ridge_freqs)
        bg_med = float(np.median(background)) if background.size else 0.0
        bg_mad = float(np.median(np.abs(background - bg_med))) if background.size else 0.0
        bg_sigma = 1.4826 * bg_mad if bg_mad > 1e-12 else float(np.std(background) + 1e-12)
        raw_snr = float((np.mean(ridge_values) - bg_med) / max(bg_sigma, 1e-9))
        peak_lift = float((np.max(ridge_values) - bg_med) / max(bg_sigma, 1e-9))
        # Chirps are often weak and short in live waterfalls; map modest positive
        # SNR to a useful [0, 1] detector feature without requiring demod-grade power.
        ridge_snr = float(np.clip(0.72 * (raw_snr / 3.0) + 0.28 * (peak_lift / 6.0), 0.0, 1.0))

        patch = self._patch_for_candidate(window, ridge_freqs)
        sharp = renyi_sharpness(patch, alpha=c.renyi_alpha)
        perm = permutation_structure_score(ridge_values, order=c.permutation_order, delay=c.permutation_delay)
        zeta = (
            zeta_resonance_score(ridge_values, c.zeta_zero_count, c.zeta_log_scales)
            if c.enable_zeta_resonance
            else 0.0
        )
        centrality = entropy_decay_centrality(window, ridge_freqs)

        raw_score = _weighted_score(
            (
                (c.ridge_snr_weight, ridge_snr),
                (c.renyi_sharpness_weight, sharp),
                (c.permutation_structure_weight, perm),
                (c.zeta_resonance_weight, zeta),
                (c.entropy_centrality_weight, centrality),
            )
        )
        # Calibrate weak-feature fusion into a user-facing confidence-like score.
        # This preserves ordering but makes modest, consistent chirp ridges visible
        # without pretending the value is a statistically calibrated probability.
        score = float(np.clip(1.0 - np.exp(-2.45 * raw_score), 0.0, 1.0))
        return _Candidate(
            score=score,
            t0=global_t0,
            t1=global_t0 + win,
            f0=float(best_idx),
            slope=float(slope),
            ridge_snr=ridge_snr,
            renyi_sharpness=sharp,
            permutation_structure=perm,
            zeta_resonance=zeta,
            entropy_centrality=centrality,
        )

    def _extract_ridge(self, window: np.ndarray, f0: float, slope: float) -> Tuple[np.ndarray, np.ndarray]:
        win, freq_bins = window.shape
        values = []
        freqs = []
        half = self.config.ridge_half_width_bins
        for ti in range(win):
            f = f0 + slope * ti
            center = int(round(f))
            if center < 0 or center >= freq_bins:
                continue
            lo = max(0, center - half)
            hi = min(freq_bins, center + half + 1)
            values.append(float(np.max(window[ti, lo:hi])))
            freqs.append(center)
        return np.asarray(values, dtype=np.float64), np.asarray(freqs, dtype=np.int64)

    def _background_for_candidate(self, window: np.ndarray, ridge_freqs: np.ndarray) -> np.ndarray:
        win, freq_bins = window.shape
        mask = np.ones_like(window, dtype=bool)
        half = max(self.config.ridge_half_width_bins + 2, 3)
        for ti, f in enumerate(ridge_freqs[:win]):
            lo = max(0, int(f) - half)
            hi = min(freq_bins, int(f) + half + 1)
            mask[ti, lo:hi] = False
        return window[mask]

    def _patch_for_candidate(self, window: np.ndarray, ridge_freqs: np.ndarray) -> np.ndarray:
        win, freq_bins = window.shape
        half = max(self.config.ridge_half_width_bins + 1, 2)
        vals = []
        for ti, f in enumerate(ridge_freqs[:win]):
            lo = max(0, int(f) - half)
            hi = min(freq_bins, int(f) + half + 1)
            vals.append(window[ti, lo:hi])
        if not vals:
            return np.array([0.0], dtype=np.float64)
        return np.concatenate([v.reshape(-1) for v in vals])

    def _candidate_to_detection(self, norm: np.ndarray, cand: _Candidate) -> ChirpDetection:
        t_len = cand.t1 - cand.t0
        local_t = np.arange(t_len, dtype=np.float64)
        freqs = cand.f0 + cand.slope * local_t
        valid = (freqs >= 0) & (freqs < norm.shape[1])
        if not np.any(valid):
            f_min = f_max = int(round(cand.f0))
        else:
            f_min = int(np.floor(np.min(freqs[valid])))
            f_max = int(np.ceil(np.max(freqs[valid])))
        pad = max(self.config.ridge_half_width_bins + 2, 3)
        f_min = max(0, f_min - pad)
        f_max = min(norm.shape[1], f_max + pad + 1)
        t0 = max(0, cand.t0)
        t1 = min(norm.shape[0], cand.t1)
        label = "riemann_resonant_chirp" if cand.zeta_resonance >= 0.25 else "entropy_chirp"
        return ChirpDetection(
            label=label,
            score=float(cand.score),
            bbox=(t0, f_min, t1, f_max),
            slope_bins_per_row=float(cand.slope),
            intercept_freq_bin=float(cand.f0),
            ridge_snr=float(cand.ridge_snr),
            renyi_sharpness=float(cand.renyi_sharpness),
            permutation_structure=float(cand.permutation_structure),
            zeta_resonance=float(cand.zeta_resonance),
            entropy_centrality=float(cand.entropy_centrality),
            time_start=t0,
            time_end=t1,
            freq_start=f_min,
            freq_end=f_max,
        )
