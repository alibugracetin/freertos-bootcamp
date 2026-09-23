"""Ham CSV'lerden rapor tablolarını üretir (AR-01…05, AR-11).

Çıktı: analysis/ozet-tablo.md  — report.md bu tabloları kullanır.
Kullanım:  python analysis/analyze.py
"""
from __future__ import annotations

import csv
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "interface"))

from protocol import SCENARIOS, stage_us          # noqa: E402
from recorder import DEADLINE_US, read_csv_records  # noqa: E402

MEAS = ROOT / "measurements"
STAGE_NAMES = ["t₁−t₀", "t₂−t₁", "t₃−t₂", "t₄−t₃"]


def diag(sc: str) -> dict[str, str]:
    p = MEAS / f"{sc}_diag.csv"
    return {r["key"]: r["value"] for r in csv.DictReader(p.open(encoding="utf-8"))} if p.exists() else {}


def pct(xs: list[float], q: float) -> float:
    xs = sorted(xs)
    k = (len(xs) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def main() -> None:
    out: list[str] = []
    w = out.append
    scen = [f"S{i}" for i in range(6) if (MEAS / f"S{i}.csv").exists()]
    data = {sc: read_csv_records(MEAS / f"{sc}.csv") for sc in scen}

    # ---- 1. Ana özet
    w("## Özet\n")
    w("| Senaryo | n | ok | eksik | R min | R ort | R medyan | R p95 | R max | >20 ms |")
    w("|---|---|---|---|---|---|---|---|---|---|")
    for sc in scen:
        recs = data[sc]
        R = [stage_us(t, 0, 4) / 1000 for _, t, st in recs if st == "ok"]
        miss = sum(1 for _, _, st in recs if st != "ok")
        w(f"| **{sc}** | {len(recs)} | {len(R)} | {miss} | {min(R):.2f} | {statistics.mean(R):.2f} | "
          f"{statistics.median(R):.2f} | {pct(R, 0.95):.2f} | **{max(R):.2f}** | "
          f"{sum(1 for r in R if r * 1000 > DEADLINE_US)} |")
    w("\nBirim: ms. Yalnızca `status = ok` olaylar; eksikler ayrı sütunda (AR-03).\n")

    # ---- 2. Aşamalar
    w("## Aşama süreleri (ok olaylar, µs)\n")
    w("| Senaryo | " + " | ".join(f"{n} ort" for n in STAGE_NAMES) + " | " +
      " | ".join(f"{n} max" for n in STAGE_NAMES) + " |")
    w("|---" * 9 + "|")
    for sc in scen:
        ok = [t for _, t, st in data[sc] if st == "ok"]
        st_ = [[stage_us(t, a, a + 1) for t in ok] for a in range(4)]
        w(f"| **{sc}** | " + " | ".join(f"{statistics.mean(s):,.0f}".replace(",", " ") for s in st_) +
          " | " + " | ".join(f"{max(s):,.0f}".replace(",", " ") for s in st_) + " |")
    w("")

    # ---- 3. Kayıplar ve sayaçlar
    w("## Kayıplar, sayaçlar ve koşu koşulları (MR-09, AR-04, AR-11)\n")
    w("| Senaryo | tx_drop | btnq_drop | tx_error | timeout | txq_hwm | TEL üretildi | "
      "TEL periyodu ort (min–max) | İş süresi ort | debounce_rej | unarmed_rej | idle_press |")
    w("|---" * 12 + "|")
    for sc in scen:
        d, recs = diag(sc), data[sc]
        cnt = lambda s: sum(1 for _, _, x in recs if x == s)
        per = f"{d.get('tel_period_avg_us','—')} ({d.get('tel_period_min_us','—')}–{d.get('tel_period_max_us','—')})"
        w(f"| **{sc}** | {cnt('tx_drop')} | {cnt('btnq_drop')} | {cnt('tx_error')} | {cnt('timeout')} | "
          f"{d.get('cnt_txq_hwm','—')} | {d.get('tel_sent','—')} | {per} | {d.get('work_avg_us','—')} | "
          f"{d.get('cni_debounce_rej','—')} | {d.get('cni_unarmed_rej','—')} | {d.get('cni_idle_press','—')} |")
    w("\nSüreler µs. `unarmed_rej`: bırakış zıplaması (FR-12b ile engellendi). "
      "`idle_press`: ölçüm dışında (ısınmada) gelen basış, kayda alınmadı.\n")

    # ---- 4. Basış aralıkları (MR-02 kanıtı)
    w("## Basış aralıkları (MR-02: ≥ 500 ms, değişken)\n")
    w("| Senaryo | min | ortalama | max | 500 ms altı |")
    w("|---|---|---|---|---|")
    for sc in scen:
        t0 = [t[0] for _, t, _ in data[sc] if t[0] is not None]
        gaps = [((b - a) & 0xFFFFFFFF) / 1000 for a, b in zip(t0, t0[1:])]
        w(f"| **{sc}** | {min(gaps):,.0f} ms | {statistics.mean(gaps):,.0f} ms | {max(gaps):,.0f} ms | "
          f"{sum(1 for g in gaps if g < 500)} / {len(gaps)} |")
    w("")

    # ---- 5. Kuyruk gecikmesinin zaman içinde gelişimi (S5 aşırı yük kanıtı)
    if "S5" in scen:
        w("## S5: kuyruk gecikmesinin olay sırasına göre gelişimi\n")
        w("| Olay sırası | t₃−t₂ (ms) | R (ms) | durum |")
        w("|---|---|---|---|")
        for i, (eid, t, st) in enumerate(sorted(data["S5"], key=lambda r: r[0]), 1):
            q = stage_us(t, 2, 3)
            R = stage_us(t, 0, 4)
            w(f"| {i} | {'—' if q is None else f'{q/1000:.1f}'} | {'—' if R is None else f'{R/1000:.1f}'} | {st} |")
        w("")

    path = ROOT / "analysis" / "ozet-tablo.md"
    path.write_text("# Ölçüm özet tabloları\n\n*Bu dosya `analysis/analyze.py` tarafından üretilir; elle düzenlenmez.*\n\n"
                    + "\n".join(out), encoding="utf-8")
    print(f"yazıldı: {path}")
    print("\n".join(out[:40]))


if __name__ == "__main__":
    main()
