from pathlib import Path

from riemann_chirp_overlay.cli import main


def test_demo_cli(tmp_path: Path):
    rc = main([
        "demo",
        "--out-dir",
        str(tmp_path),
        "--time-bins",
        "96",
        "--freq-bins",
        "192",
        "--window-time-bins",
        "48",
        "--window-step-bins",
        "16",
        "--min-score",
        "0.40",
    ])
    assert rc == 0
    assert (tmp_path / "waterfall.npy").exists()
    assert (tmp_path / "detections.json").exists()
    assert (tmp_path / "overlay.png").exists()
