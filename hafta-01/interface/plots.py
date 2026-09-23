"""Grafiklerin tek kaynağı (UI-20…28, AR-06…08).\n\nHer fonksiyon **yalnızca CSV'den** okur ve bir matplotlib ``Figure`` döndürür;\ndosya yazmaz, pencere açmaz. Tüketicileri:\n\n- ``gui.py``        — ``FigureCanvasTkAgg`` ile pencereye gömer\n- ``make_plots.py`` — PNG olarak ``analysis/plots/`` altına kaydeder\n\nBöylece ekranda görülen grafik ile teslim edilen PNG aynı koddan ve aynı ham\nveriden gelir (MR-08).\n\nRenk kuralı: renk yalnızca **aşamaları** kodlar (4 kategorik slot, sabit sıra).\nSenaryolar renkle değil konumla (x ekseni, panel başlığı) ayırt edilir.\nPalet: dataviz referans paletinin ilk dört slotu, belgelenmiş sırayla ve\ndeğiştirilmeden (yan yana çiftler doğrulanmış). Açık yüzeyde sarı < 3:1\nkontrast olduğu için lejant daima görünür ve değerler tablo olarak da verilir.\n"""
from __future__ import annotations

import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from matplotlib.figure import Figure

from protocol import SCENARIOS, stage_us
from recorder import DEADLINE_US, read_csv_records

# ---- palet ve mürekkep (dataviz referans paleti, açık tema)
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]           # slot 1–4: mavi, turuncu, su yeşili, sarı
SERIES = SLOTS[0]                                              # tek seri → slot 1
# Aşama → slot eşlemesi TERS sırada: t₁−t₀ → 4 … t₄−t₃ → 1. Yığında yan yana
# gelen çiftler yine (1,2), (2,3), (3,4) olur — doğrulanmış komşuluklar korunur.
# Böylece baskın hat süresi en yüksek kontrastlı slota (mavi), yükle değişen
# kuyruk beklemesi dikkat çeken slota (turuncu) düşer; sarı en küçük aşamada kalır.
STAGE_COLORS = [SLOTS[3], SLOTS[2], SLOTS[1], SLOTS[0]]

#: x ekseni için kısa senaryo etiketleri (uzun açıklamalar üst üste biner)
SHORT = {"S0": "telemetri\nkapalı", "S1": "10 Hz", "S2": "50 Hz",
         "S3": "100 Hz", "S4": "100 Hz\n+2 ms CPU", "S5": "100 Hz\n+5 ms CPU"}

STAGES = [
    ("t₁−t₀", "görev bekleme"),
    ("t₂−t₁", "yanıt hazırlama"),
    ("t₃−t₂", "TX kuyruğu + başlatma"),
    ("t₄−t₃", "UART hattı + TC"),
]


@dataclass
class ScenarioData:
    scenario: str
    event_ids: list[int]
    R_ms: list[float]                       # yalnızca status == ok
    stages_us: list[list[int]]              # [aşama][olay], yalnızca ok
    excluded: dict[str, int]                # status → sayı (ok dışı)

    @property
    def n_ok(self) -> int:
        return len(self.R_ms)

    def excluded_text(self) -> str:
        if not self.excluded:
            return "0 dışlandı"
        parts = ", ".join(f"{n} {st}" for st, n in sorted(self.excluded.items()))
        return f"{sum(self.excluded.values())} dışlandı: {parts}"


def load(path: Path) -> ScenarioData:
    """Bir Sx.csv dosyasını okur; ok olmayan satırları dışlar ama sayar (UI-27)."""
    recs = read_csv_records(path)
    ok = [(eid, t) for eid, t, st in recs if st == "ok"]
    excluded: dict[str, int] = {}
    for _, _, st in recs:
        if st != "ok":
            excluded[st] = excluded.get(st, 0) + 1
    return ScenarioData(
        scenario=path.stem,
        event_ids=[e for e, _ in ok],
        R_ms=[stage_us(t, 0, 4) / 1000 for _, t in ok],
        stages_us=[[stage_us(t, a, a + 1) for _, t in ok] for a in range(4)],
        excluded=excluded,
    )


def available(folder: Path) -> list[Path]:
    """Klasördeki S0…S5.csv dosyaları, senaryo sırasıyla."""
    return [folder / f"S{i}.csv" for i in range(6) if (folder / f"S{i}.csv").exists()]


def _style(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(AXIS)
    ax.tick_params(colors=MUTED, labelcolor=INK_2, labelsize=8)
    ax.grid(axis="y", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def _empty(fig: Figure, text: str) -> Figure:
    ax = fig.add_subplot(111)
    ax.set_axis_off()
    ax.text(0.5, 0.5, text, ha="center", va="center", color=INK_2, fontsize=11, wrap=True)
    return fig


# ------------------------------------------------------------ grafik 1 (AR-06a)

def plot_response_times(csv_paths: list[Path], title_note: str = "") -> Figure:
    """Olay numarası → R [ms], senaryo başına bir panel, 20 ms deadline çizgisi.\n\nPaneller ortak y eksenini paylaşır; senaryolar arası karşılaştırma aynı ölçekte.\nHer noktada ``_hover`` bilgisi vardır (gui.py fare üstü ipucu için okur).\n"""
    fig = Figure(figsize=(9, 5.2), dpi=100, facecolor=SURFACE)
    data = [load(p) for p in csv_paths]
    data = [d for d in data if d.n_ok or d.excluded]
    if not data:
        return _empty(fig, "Çizilecek ölçüm yok.\nBir senaryoyu tamamlayın (hedef olay sayısına ulaşın).")

    # Ortak y ekseni kullanılmaz: S5, S0–S4'ten ~20 kat büyüktür ve ortak ölçekte
    # diğerlerinin dağılımı tamamen ezilir. Her panelin kendi ölçeği vardır; 20 ms
    # deadline çizgisi her panelde göründüğü için karşılaştırma yine mümkündür.
    cols = min(3, len(data))
    rows = (len(data) + cols - 1) // cols
    axes = fig.subplots(rows, cols, squeeze=False)

    for k, ax in enumerate(axes.flat):
        if k >= len(data):
            ax.set_visible(False)
            continue
        d = data[k]
        _style(ax)
        x = list(range(1, d.n_ok + 1))
        ax.plot(x, d.R_ms, color=SERIES, linewidth=1.2, alpha=0.55, zorder=2)
        pts = ax.scatter(x, d.R_ms, s=26, color=SERIES, edgecolors=SURFACE, linewidths=1.2, zorder=3)
        pts._hover = [f"{d.scenario} · olay {eid}\nR = {r:.3f} ms" for eid, r in zip(d.event_ids, d.R_ms)]

        ax.axhline(DEADLINE_US / 1000, color=INK_2, linestyle=(0, (4, 3)), linewidth=1.0, zorder=1)
        ax.text(0.02, DEADLINE_US / 1000, "20 ms deadline", transform=ax.get_yaxis_transform(),
                va="bottom", ha="left", fontsize=7, color=INK_2)

        over = sum(1 for r in d.R_ms if r * 1000 > DEADLINE_US)
        ax.set_title(f"{d.scenario} · {SCENARIOS.get(d.scenario, '')}", fontsize=9.5, color=INK,
                     loc="left", pad=15)
        ax.text(0, 1.015, f"n = {d.n_ok} · {d.excluded_text()} · aşım: {over}",
                transform=ax.transAxes, fontsize=7.5, color=MUTED, va="bottom")
        ax.set_ylim(0, max(DEADLINE_US / 1000 * 1.15, (max(d.R_ms) if d.R_ms else 0) * 1.12))
        ax.set_xlim(0.3, max(d.n_ok, 1) + 0.7)
        ax.set_ylabel("R = t₄ − t₀  [ms]", fontsize=8.5, color=INK_2)
        if k // cols == rows - 1:
            ax.set_xlabel("olay sırası", fontsize=8.5, color=INK_2)

    fig.suptitle("Buton yanıt süresi — olay başına" + (f"  ({title_note})" if title_note else ""),
                 x=0.01, ha="left", fontsize=11.5, color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.96), h_pad=2.2)
    return fig


# ------------------------------------------------------------ grafik 2 (AR-06b)

def stage_means(csv_paths: list[Path]) -> list[tuple[str, int, list[float]]]:
    """[(senaryo, n_ok, [4 aşamanın ortalaması µs])] — tablo görünümü için de kullanılır."""
    out = []
    for p in csv_paths:
        d = load(p)
        if d.n_ok:
            out.append((d.scenario, d.n_ok, [statistics.mean(s) for s in d.stages_us]))
    return out


def plot_stage_breakdown(csv_paths: list[Path], title_note: str = "") -> Figure:
    """Senaryo → aşama ortalamaları [ms], yığılmış sütun. Tepede ortalama R.\n\nAşama renkleri sabit sıradadır; bir senaryo eksik olsa da renk değişmez.\nBir senaryo diğerlerinden çok büyükse (aşırı yük rejimi) ikinci bir **yakın\nplan** paneli çizilir: sütunlar kırpılmaz, küçük senaryolar da okunur kalır.\n"""
    fig = Figure(figsize=(9.6, 5.2), dpi=100, facecolor=SURFACE)
    rows = stage_means(csv_paths)
    if not rows:
        return _empty(fig, "Çizilecek ölçüm yok.\nBir senaryoyu tamamlayın (hedef olay sayısına ulaşın).")

    totals = [sum(r[2]) / 1000 for r in rows]
    zoom = [r for r, t in zip(rows, totals) if t < 0.25 * max(totals)] if max(totals) > 4 * min(totals) else []
    axes = fig.subplots(1, 2, width_ratios=[1.35, 1]) if len(zoom) >= 2 else [fig.add_subplot(111)]

    for ax, subset, note in zip(axes, [rows, zoom], ["tümü", "yakın plan"]):
        _style(ax)
        x = list(range(len(subset)))
        bottom = [0.0] * len(subset)
        for si, ((sym, name), color) in enumerate(zip(STAGES, STAGE_COLORS)):
            vals = [r[2][si] / 1000 for r in subset]
            bars = ax.bar(x, vals, bottom=bottom, width=0.56, color=color,
                          edgecolor=SURFACE, linewidth=1.5, label=f"{sym}  {name}", zorder=2)
            for bar, (sc, n, means) in zip(bars, subset):
                bar._hover = f"{sc} · {sym} {name}\nortalama {means[si]:,.0f} µs".replace(",", " ")
            bottom = [b + v for b, v in zip(bottom, vals)]

        for xi, total in zip(x, bottom):
            ax.text(xi, total, f"{total:.2f}", ha="center", va="bottom", fontsize=8, color=INK, zorder=3)

        top = max(bottom) * 1.18
        if DEADLINE_US / 1000 < top:
            ax.axhline(DEADLINE_US / 1000, color=INK_2, linestyle=(0, (4, 3)), linewidth=1.0, zorder=1)
            ax.text(0.02, DEADLINE_US / 1000, "20 ms deadline", transform=ax.get_yaxis_transform(),
                    va="bottom", ha="left", fontsize=7, color=INK_2)
        ax.set_ylim(0, top)
        ax.set_xticks(x, [f"{sc}\n{SHORT.get(sc, '')}\nn = {n}" for sc, n, _ in subset], fontsize=7.5)
        ax.set_ylabel("ortalama süre [ms]", fontsize=8.5, color=INK_2)
        if len(axes) > 1:
            ax.set_title(note if note == "tümü" else f"{note} ({', '.join(r[0] for r in subset)})",
                         fontsize=9, color=INK_2, loc="left")

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles[::-1], labels[::-1], loc="upper right", bbox_to_anchor=(0.995, 0.93),
               fontsize=8, frameon=False, labelcolor=INK_2, title="aşama (yalnızca status = ok)",
               title_fontsize=8, alignment="left")

    fig.suptitle("Yanıt süresinin aşamalara dağılımı — senaryo ortalamaları [ms]"
                 + (f"  ({title_note})" if title_note else ""), x=0.01, ha="left", fontsize=11.5, color=INK)
    fig.tight_layout(rect=(0, 0, 0.78, 0.94))
    return fig


def figure_hover_text(fig: Figure, event) -> Optional[str]:
    """Fare altındaki işaretin ``_hover`` metni (yoksa None). GUI ipucu için."""
    for ax in fig.axes:
        if event.inaxes is not ax:
            continue
        for coll in ax.collections:
            texts = getattr(coll, "_hover", None)
            if texts:
                hit, info = coll.contains(event)
                if hit and len(info.get("ind", [])):
                    return texts[info["ind"][0]]
        for patch in ax.patches:
            text = getattr(patch, "_hover", None)
            if text and patch.contains(event)[0]:
                return text
    return None
