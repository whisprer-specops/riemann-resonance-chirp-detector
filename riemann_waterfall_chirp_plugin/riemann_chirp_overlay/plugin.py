from __future__ import annotations

from dataclasses import dataclass
from typing import List

import numpy as np

from .config import RiemannChirpConfig
from .detector import RiemannChirpDetector
from .overlay import build_overlay_rgba
from .types import ChirpDetection


@dataclass(frozen=True)
class PluginFrameResult:
    detections: List[ChirpDetection]
    overlay_rgba: np.ndarray
    heatmap: np.ndarray

    def detections_as_dicts(self) -> list[dict]:
        return [det.to_dict() for det in self.detections]


class RiemannResonanceWaterfallPlugin:
    """Stable host-facing plugin façade.

    GUI/waterfall applications should integrate this class, not the internal
    detector implementation.
    """

    def __init__(self, config: RiemannChirpConfig | None = None) -> None:
        self.config = config or RiemannChirpConfig()
        self.config.validate()
        self.detector = RiemannChirpDetector(self.config)

    def process_waterfall_frame(self, frame: np.ndarray) -> PluginFrameResult:
        detections = self.detector.detect(frame)
        heatmap = self.detector.heatmap(frame)
        overlay = build_overlay_rgba(frame.shape, detections, self.config)
        return PluginFrameResult(detections=detections, overlay_rgba=overlay, heatmap=heatmap)
