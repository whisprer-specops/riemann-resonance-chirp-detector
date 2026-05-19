from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any, Dict, Tuple


BBox = Tuple[int, int, int, int]


@dataclass(frozen=True)
class ChirpDetection:
    """A detected chirp-like feature in a waterfall frame."""

    label: str
    score: float
    bbox: BBox
    slope_bins_per_row: float
    intercept_freq_bin: float
    ridge_snr: float
    renyi_sharpness: float
    permutation_structure: float
    zeta_resonance: float
    entropy_centrality: float
    time_start: int
    time_end: int
    freq_start: int
    freq_end: int

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["bbox"] = list(self.bbox)
        return d
