from riemann_chirp_overlay import RiemannChirpConfig, RiemannResonanceWaterfallPlugin
from riemann_chirp_overlay.synthetic import SyntheticWaterfallSpec, make_synthetic_waterfall


def test_detector_finds_synthetic_chirps():
    frame, _truth = make_synthetic_waterfall(SyntheticWaterfallSpec(time_bins=192, freq_bins=384, seed=42))
    config = RiemannChirpConfig(
        window_time_bins=80,
        window_step_bins=20,
        min_score=0.45,
        max_detections=16,
        slope_count_per_direction=36,
    )
    result = RiemannResonanceWaterfallPlugin(config).process_waterfall_frame(frame)
    assert len(result.detections) >= 2
    assert result.overlay_rgba.shape == (192, 384, 4)
    assert result.heatmap.shape == (192, 384)
    assert all(0.0 <= det.score <= 1.0 for det in result.detections)
