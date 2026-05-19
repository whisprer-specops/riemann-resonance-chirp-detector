from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np

from .config import RiemannChirpConfig
from .overlay import alpha_blend_grayscale
from .plugin import RiemannResonanceWaterfallPlugin
from .synthetic import SyntheticWaterfallSpec, make_synthetic_waterfall


def _save_png(path: Path, rgb: np.ndarray) -> None:
    try:
        import matplotlib.pyplot as plt
    except Exception as exc:  # pragma: no cover - only hit when optional dep absent
        raise RuntimeError("saving PNG requires matplotlib; install with .[demo]") from exc
    path.parent.mkdir(parents=True, exist_ok=True)
    plt.imsave(path, rgb)


def _config_from_args(args: argparse.Namespace) -> RiemannChirpConfig:
    return RiemannChirpConfig(
        min_score=args.min_score,
        window_time_bins=args.window_time_bins,
        window_step_bins=args.window_step_bins,
        max_detections=args.max_detections,
        enable_zeta_resonance=not args.no_zeta,
    )


def scan_command(args: argparse.Namespace) -> int:
    in_path = Path(args.input)
    frame = np.load(in_path)
    config = _config_from_args(args)
    plugin = RiemannResonanceWaterfallPlugin(config)
    result = plugin.process_waterfall_frame(frame)

    detections_path = Path(args.detections)
    detections_path.parent.mkdir(parents=True, exist_ok=True)
    detections_path.write_text(json.dumps(result.detections_as_dicts(), indent=2), encoding="utf-8")

    if args.heatmap:
        heatmap_path = Path(args.heatmap)
        heatmap_path.parent.mkdir(parents=True, exist_ok=True)
        np.save(heatmap_path, result.heatmap)

    if args.overlay:
        rgb = alpha_blend_grayscale(frame, result.overlay_rgba)
        _save_png(Path(args.overlay), rgb)

    print(f"detections: {len(result.detections)}")
    print(f"wrote: {detections_path}")
    return 0


def demo_command(args: argparse.Namespace) -> int:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    frame, truth = make_synthetic_waterfall(
        SyntheticWaterfallSpec(time_bins=args.time_bins, freq_bins=args.freq_bins, seed=args.seed)
    )
    waterfall_path = out_dir / "waterfall.npy"
    np.save(waterfall_path, frame)

    config = _config_from_args(args)
    plugin = RiemannResonanceWaterfallPlugin(config)
    result = plugin.process_waterfall_frame(frame)

    (out_dir / "truth.json").write_text(json.dumps(truth, indent=2), encoding="utf-8")
    (out_dir / "detections.json").write_text(json.dumps(result.detections_as_dicts(), indent=2), encoding="utf-8")
    np.save(out_dir / "heatmap.npy", result.heatmap)
    _save_png(out_dir / "overlay.png", alpha_blend_grayscale(frame, result.overlay_rgba))

    print(f"wrote: {waterfall_path}")
    print(f"detections: {len(result.detections)}")
    print(f"overlay: {out_dir / 'overlay.png'}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="rrchirp", description="Riemann-resonance SDR waterfall chirp highlighter")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--min-score", type=float, default=0.34)
        p.add_argument("--window-time-bins", type=int, default=96)
        p.add_argument("--window-step-bins", type=int, default=24)
        p.add_argument("--max-detections", type=int, default=32)
        p.add_argument("--no-zeta", action="store_true", help="disable zeta-zero resonance feature")

    p_scan = sub.add_parser("scan", help="scan an existing .npy waterfall frame")
    p_scan.add_argument("input", help="2D NumPy .npy waterfall array, shape (time, frequency)")
    p_scan.add_argument("--detections", default="detections.json")
    p_scan.add_argument("--overlay", default="overlay.png")
    p_scan.add_argument("--heatmap", default="heatmap.npy")
    add_common(p_scan)
    p_scan.set_defaults(func=scan_command)

    p_demo = sub.add_parser("demo", help="generate a synthetic waterfall and scan it")
    p_demo.add_argument("--out-dir", default="demo_out")
    p_demo.add_argument("--time-bins", type=int, default=256)
    p_demo.add_argument("--freq-bins", type=int, default=512)
    p_demo.add_argument("--seed", type=int, default=1337)
    add_common(p_demo)
    p_demo.set_defaults(func=demo_command)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
