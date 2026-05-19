from __future__ import annotations

from pathlib import Path

from riemann_chirp_overlay.cli import main


if __name__ == "__main__":
    out = Path(__file__).resolve().parent / "demo_out"
    raise SystemExit(main(["demo", "--out-dir", str(out)]))
