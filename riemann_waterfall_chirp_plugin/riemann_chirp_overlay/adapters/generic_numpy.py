from __future__ import annotations

import numpy as np

from ..config import RiemannChirpConfig
from ..plugin import PluginFrameResult, RiemannResonanceWaterfallPlugin


class GenericNumpyWaterfallAdapter:
    """Tiny adapter for hosts that can pass a NumPy waterfall frame.

    This is intentionally boring and stable. Most GUI SDR applications can call a
    Python function, exchange shared-memory arrays, or talk to a sidecar that
    presents this same API.
    """

    def __init__(self, config: RiemannChirpConfig | None = None) -> None:
        self.plugin = RiemannResonanceWaterfallPlugin(config)

    def process(self, frame_time_by_frequency: np.ndarray) -> PluginFrameResult:
        return self.plugin.process_waterfall_frame(frame_time_by_frequency)
