"""Deney sonu dökümünü (REC/TST/CNI/CNT/END) toplar ve CSV dosyalarına yazar.

Dosyalar (``measurements/``):

- ``Sx.csv``       — her satır bir buton olayı (MR-20); alınamamış damga boş (MR-21)
- ``Sx_diag.csv``  — o koşunun telemetri istatistiği ve sayaçları (anahtar, değer)
- ``summary.csv``  — mevcut tüm Sx.csv dosyalarından yeniden üretilir (UI-09, AR-01…05, AR-11)

Kurallar:

- Var olan bir ölçüm dosyasının **üzerine yazılmaz**; eskisi ``arsiv/`` altına
  zaman damgasıyla taşınır. Ham veri tek nüshadır.
- Hedefi 30'dan az olan koşular (MR-01'i karşılamaz) ``deneme/`` altına yazılır;
  resmî ölçüm klasörünü kirletmez.
- Beklenen kayıt sayısı tutmazsa yazmadan önce uyarı üretilir (UI-14).
"""
from __future__ import annotations

import csv
import shutil
import statistics
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Optional

from protocol import CNI_FIELDS, CNT_FIELDS, TST_FIELDS, Counters, End, Rec, TelStats, stage_us

MEASUREMENTS = Path(__file__).resolve().parent.parent / "measurements"
MIN_OFFICIAL = 30          # MR-01
DEADLINE_US = 20_000       # ödev deadline'ı

CSV_HEADER = ["scenario", "event_id", "t0_us", "t1_us", "t2_us", "t3_us", "t4_us", "status"]


@dataclass
class DumpResult:
    scenario: str
    csv_path: Path
    n_records: int
    warnings: list[str] = field(default_factory=list)


class DumpCollector:
    """Bir DUMP akışını toplar; END gelince dosyaları yazar."""

    def __init__(self, scenario: str, expected_events: int, target: int) -> None:
        self.scenario = scenario
        self.expected = expected_events
        self.target = target
        self.records: list[Rec] = []
        self.tel: Optional[TelStats] = None
        self.counters: dict[str, dict[str, int]] = {}

    def feed(self, msg: object) -> Optional[DumpResult]:
        """Bir mesaj besler. END geldiyse dosyaları yazar ve sonucu döndürür."""
        if isinstance(msg, Rec):
            self.records.append(msg)
        elif isinstance(msg, TelStats):
            self.tel = msg
        elif isinstance(msg, Counters):
            self.counters[msg.kind] = msg.values
        elif isinstance(msg, End):
            return self._finish(msg)
        return None

    def _finish(self, end: End) -> DumpResult:
        warnings = []
        if end.n != len(self.records):
            warnings.append(f"Kart {end.n} kayıt bildirdi, {len(self.records)} alındı — satır kaybı var.")
        if len(self.records) != self.expected:
            warnings.append(f"Beklenen {self.expected} olay, {len(self.records)} kayıt geldi.")

        official = self.target >= MIN_OFFICIAL
        folder = MEASUREMENTS if official else MEASUREMENTS / "deneme"
        folder.mkdir(parents=True, exist_ok=True)
        if not official:
            warnings.append(f"Hedef {self.target} < {MIN_OFFICIAL}: deneme koşusu, {folder.name}/ altına yazıldı.")

        csv_path = folder / f"{self.scenario}.csv"
        diag_path = folder / f"{self.scenario}_diag.csv"
        _archive(csv_path)
        _archive(diag_path)

        rows = sorted(self.records, key=lambda r: r.event_id)
        with csv_path.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(CSV_HEADER)
            for r in rows:
                w.writerow([r.scenario, r.event_id, *["" if v is None else v for v in r.t], r.status])

        with diag_path.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["key", "value"])
            w.writerow(["scenario", self.scenario])
            w.writerow(["target", self.target])
            w.writerow(["written_at", datetime.now().isoformat(timespec="seconds")])
            if self.tel:
                for k in TST_FIELDS:
                    w.writerow([k, self.tel.values.get(k, "")])
            for kind, names in (("CNI", CNI_FIELDS), ("CNT", CNT_FIELDS)):
                vals = self.counters.get(kind, {})
                for k in names:
                    w.writerow([f"{kind.lower()}_{k}", vals.get(k, "")])

        if official:
            rebuild_summary(MEASUREMENTS)
        return DumpResult(self.scenario, csv_path, len(rows), warnings)


def _archive(path: Path) -> None:
    """Var olan dosyayı ``arsiv/`` altına zaman damgasıyla taşır (üzerine yazmaz)."""
    if path.exists():
        dest = path.parent / "arsiv"
        dest.mkdir(exist_ok=True)
        stamp = datetime.fromtimestamp(path.stat().st_mtime).strftime("%Y%m%d_%H%M%S")
        target = dest / f"{path.stem}_{stamp}{path.suffix}"
        n = 1
        while target.exists():                     # aynı saniyede ikinci arşiv
            target = dest / f"{path.stem}_{stamp}_{n}{path.suffix}"
            n += 1
        shutil.move(str(path), target)


# ------------------------------------------------------------------- özet tablo

def read_csv_records(path: Path) -> list[tuple[int, list[Optional[int]], str]]:
    """Sx.csv → [(event_id, [t0..t4], status)]."""
    out = []
    with path.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            t = [int(row[k]) if row[k] else None for k in CSV_HEADER[2:7]]
            out.append((int(row["event_id"]), t, row["status"]))
    return out


def _read_diag(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    with path.open(encoding="utf-8") as f:
        return {row["key"]: row["value"] for row in csv.DictReader(f)}


def rebuild_summary(folder: Path) -> Path:
    """Klasördeki S0…S5.csv dosyalarından summary.csv'yi yeniden üretir."""
    out = folder / "summary.csv"
    cols = ["scenario", "n_total", "n_ok", "n_missing",
            "R_min_ms", "R_avg_ms", "R_max_ms", "n_over_20ms",
            "t1_t0_avg_us", "t2_t1_avg_us", "t3_t2_avg_us", "t4_t3_avg_us",
            "btnq_drop", "tx_drop", "tx_error", "timeout",
            "tel_sent", "tel_period_avg_us", "tel_period_min_us", "tel_period_max_us",
            "work_avg_us", "txq_hwm", "debounce_rej", "unarmed_rej"]
    rows = []
    for i in range(6):
        sc = f"S{i}"
        p = folder / f"{sc}.csv"
        if not p.exists():
            continue
        recs = read_csv_records(p)
        diag = _read_diag(folder / f"{sc}_diag.csv")
        ok = [t for _, t, st in recs if st == "ok"]
        R = [stage_us(t, 0, 4) / 1000 for t in ok]
        stages = [[stage_us(t, a, a + 1) for t in ok] for a in range(4)]
        count = lambda s: sum(1 for _, _, st in recs if st == s)
        rows.append({
            "scenario": sc, "n_total": len(recs), "n_ok": len(ok), "n_missing": len(recs) - len(ok),
            "R_min_ms": f"{min(R):.3f}" if R else "", "R_avg_ms": f"{statistics.mean(R):.3f}" if R else "",
            "R_max_ms": f"{max(R):.3f}" if R else "",
            # Tamamlanmamış yanıtlar "deadline karşılandı" sayılmaz (AR-03): yalnızca ok'lar sayılır,
            # eksikler n_missing'de ayrıca raporlanır.
            "n_over_20ms": sum(1 for r in R if r * 1000 > DEADLINE_US),
            **{f"{n}_avg_us": f"{statistics.mean(s):.1f}" if s else ""
               for n, s in zip(["t1_t0", "t2_t1", "t3_t2", "t4_t3"], stages)},
            "btnq_drop": count("btnq_drop"), "tx_drop": count("tx_drop"),
            "tx_error": count("tx_error"), "timeout": count("timeout"),
            "tel_sent": diag.get("tel_sent", ""), "tel_period_avg_us": diag.get("tel_period_avg_us", ""),
            "tel_period_min_us": diag.get("tel_period_min_us", ""), "tel_period_max_us": diag.get("tel_period_max_us", ""),
            "work_avg_us": diag.get("work_avg_us", ""), "txq_hwm": diag.get("cnt_txq_hwm", ""),
            "debounce_rej": diag.get("cni_debounce_rej", ""), "unarmed_rej": diag.get("cni_unarmed_rej", ""),
        })
    with out.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    return out
