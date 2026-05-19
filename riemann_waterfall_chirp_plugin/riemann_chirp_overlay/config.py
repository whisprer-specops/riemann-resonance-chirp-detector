from __future__ import annotations

from dataclasses import dataclass, field
from typing import Sequence


@dataclass(frozen=True)
class RiemannChirpConfig:
    """Configuration for the waterfall chirp highlighter.

    Coordinates use waterfall bin units. Input frames are expected to have shape
    ``(time_bins, frequency_bins)``.
    """

    # Sliding window geometry.
    window_time_bins: int = 96
    window_step_bins: int = 24

    # Chirp slope search, in frequency bins per time bin.
    min_abs_slope_bins_per_row: float = 0.04
    max_abs_slope_bins_per_row: float = 1.25
    slope_count_per_direction: int = 48

    # Detector thresholds and NMS.
    min_score: float = 0.34
    max_detections: int = 32
    nms_iou_threshold: float = 0.22
    ridge_half_width_bins: int = 2

    # Robust normalization.
    highpass_time_rows: int = 5
    robust_clip_sigma: float = 7.5

    # Feature weights; should sum near 1.0 but exact sum is normalized at runtime.
    ridge_snr_weight: float = 0.42
    renyi_sharpness_weight: float = 0.22
    permutation_structure_weight: float = 0.13
    zeta_resonance_weight: float = 0.17
    entropy_centrality_weight: float = 0.06

    # Entropy feature parameters.
    renyi_alpha: float = 2.0
    permutation_order: int = 4
    permutation_delay: int = 1

    # Zeta-zero feature bank.
    zeta_zero_count: int = 24
    zeta_log_scales: Sequence[float] = field(default_factory=lambda: (0.40, 0.70, 1.00, 1.45, 2.10))
    enable_zeta_resonance: bool = True

    # Overlay rendering.
    overlay_alpha: float = 0.58
    draw_ridge: bool = True
    draw_boxes: bool = True

    def validate(self) -> None:
        if self.window_time_bins < 12:
            raise ValueError("window_time_bins must be >= 12")
        if self.window_step_bins < 1:
            raise ValueError("window_step_bins must be >= 1")
        if self.min_abs_slope_bins_per_row < 0:
            raise ValueError("min_abs_slope_bins_per_row must be non-negative")
        if self.max_abs_slope_bins_per_row <= self.min_abs_slope_bins_per_row:
            raise ValueError("max_abs_slope_bins_per_row must exceed min_abs_slope_bins_per_row")
        if self.slope_count_per_direction < 2:
            raise ValueError("slope_count_per_direction must be >= 2")
        if not (0.0 <= self.min_score <= 1.0):
            raise ValueError("min_score must be in [0, 1]")
        if self.max_detections < 1:
            raise ValueError("max_detections must be >= 1")
        if self.ridge_half_width_bins < 0:
            raise ValueError("ridge_half_width_bins must be >= 0")
        if self.renyi_alpha <= 0 or abs(self.renyi_alpha - 1.0) < 1e-9:
            raise ValueError("renyi_alpha must be > 0 and != 1")
        if self.permutation_order < 3:
            raise ValueError("permutation_order must be >= 3")
        if self.permutation_delay < 1:
            raise ValueError("permutation_delay must be >= 1")
        if self.zeta_zero_count < 1:
            raise ValueError("zeta_zero_count must be >= 1")
        if not self.zeta_log_scales:
            raise ValueError("zeta_log_scales must not be empty")
        if not (0.0 <= self.overlay_alpha <= 1.0):
            raise ValueError("overlay_alpha must be in [0, 1]")
