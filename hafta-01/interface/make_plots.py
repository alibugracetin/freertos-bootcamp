"""Zorunlu iki grafiği CSV'lerden üretip PNG olarak kaydeder (AR-06…08).

Arayüzle aynı çizim kodunu (``plots.py``) kullanır; eğitmen grafikleri
arayüzü açmadan yeniden üretebilir.

Kullanım:
    python make_plots.py                       # measurements/  → analysis/plots/
    python make_plots.py --src ../measurements/deneme
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import matplotlib  # noqa: E402

matplotlib.use("Agg")                                   # pencere açma

import plots  # noqa: E402
from recorder import MEASUREMENTS  # noqa: E402

PLOTS_DIR = Path(__file__).resolve().parent.parent / "analysis" / "plots"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--src", type=Path, default=MEASUREMENTS, help="Sx.csv dosyalarının klasörü")
    ap.add_argument("--out", type=Path, default=PLOTS_DIR, help="PNG çıktı klasörü")
    args = ap.parse_args()

    paths = plots.available(args.src)
    if not paths:
        sys.exit(f"{args.src} içinde S0…S5.csv yok.")
    note = "deneme" if args.src.name == "deneme" else ""
    args.out.mkdir(parents=True, exist_ok=True)

    for name, fn in (("r_per_event", plots.plot_response_times),
                     ("stage_breakdown", plots.plot_stage_breakdown)):
        out = args.out / (f"{name}_deneme.png" if note else f"{name}.png")
        fn(paths, note).savefig(out, dpi=150, facecolor=plots.SURFACE)
        print(f"yazıldı: {out}")


if __name__ == "__main__":
    main()
