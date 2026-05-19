"""Riemann Chirp Overlay.

Software-agnostic SDR waterfall chirp highlighter using entropy and
zeta-zero resonance features.
"""

from .config import RiemannChirpConfig
from .detector import RiemannChirpDetector
from .plugin import RiemannResonanceWaterfallPlugin, PluginFrameResult
from .types import ChirpDetection

__all__ = [
    "RiemannChirpConfig",
    "RiemannChirpDetector",
    "RiemannResonanceWaterfallPlugin",
    "PluginFrameResult",
    "ChirpDetection",
]
