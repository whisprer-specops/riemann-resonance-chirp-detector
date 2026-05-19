"""GNU Radio Embedded Python Block reference adapter.

Copy the `work` method logic into an Embedded Python block when your flowgraph
already creates waterfall/spectrogram tiles. This file intentionally avoids a hard
runtime dependency on GNU Radio so the package remains installable anywhere.
"""

from __future__ import annotations

import numpy as np

from ..config import RiemannChirpConfig
from ..plugin import RiemannResonanceWaterfallPlugin


class GnuRadioWaterfallTileAdapter:
    """Reference adapter for GNU Radio-style tile processing."""

    def __init__(self, time_bins: int, freq_bins: int, config: RiemannChirpConfig | None = None) -> None:
        self.time_bins = int(time_bins)
        self.freq_bins = int(freq_bins)
        self.plugin = RiemannResonanceWaterfallPlugin(config)

    def process_flat_tile(self, flat_tile: np.ndarray) -> dict:
        frame = np.asarray(flat_tile, dtype=np.float32).reshape(self.time_bins, self.freq_bins)
        result = self.plugin.process_waterfall_frame(frame)
        return {
            "detections": result.detections_as_dicts(),
            "overlay_rgba": result.overlay_rgba,
            "heatmap": result.heatmap,
        }
