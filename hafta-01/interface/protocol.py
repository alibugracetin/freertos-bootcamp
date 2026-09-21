"""Kart ↔ PC satır protokolü: ayrıştırma ve komut üretme.

Firmware tarafı: ``firmware/Core/Src/protocol.c`` ve ``experiment.c``.
Ölçüm mesajları (TEL, BTN) tam 64 bayttır (LF dahil); kontrol satırları
değişken uzunluktadır ve yalnızca ölçüm dışında gelir (tasarım §6).

Bu modül PC saatini hiçbir yerde kullanmaz. Tüm zaman bilgisi kartın
TIM2 sayacından (µs) gelir (UI-11).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

MSG_LEN = 64  # TEL / BTN sabit uzunluğu (FR-50)

SCENARIOS: dict[str, str] = {
    "S0": "Telemetri kapalı — referans",
    "S1": "10 Hz telemetri",
    "S2": "50 Hz telemetri",
    "S3": "100 Hz telemetri",
    "S4": "100 Hz + ≈2 ms CPU işi",
    "S5": "100 Hz + ≈5 ms CPU işi",
}

# Firmware app_diag.h ile AYNI sırada (CNI = IsrCounters, CNT = TaskCounters).
CNI_FIELDS = ["accepted", "debounce_rej", "btnq_drop", "rec_ovf", "rec_mismatch", "unarmed_rej",
              "events", "idle_press", "rx_overflow", "rx_err", "cmd_busy"]
CNT_FIELDS = ["txq_drop_tel", "txq_drop_btn", "rec_mismatch", "uart_err", "uart_timeout",
              "rec_mismatch_tx", "fmt_err_tel", "fmt_err_btn", "txq_hwm", "cmd_err"]
TST_FIELDS = ["tel_sent", "tel_period_min_us", "tel_period_avg_us", "tel_period_max_us",
              "work_avg_us", "work_max_us"]


# ------------------------------------------------------------------ mesaj tipleri

@dataclass
class Tel:
    seq: int
    scenario: str
    t_us: int


@dataclass
class Btn:
    event_id: int
    scenario: str


@dataclass
class Status:
    state: str
    scenario: str
    events: int
    target: int
    period_ms: int


@dataclass
class Rec:
    scenario: str
    event_id: int
    t: list[Optional[int]]  # t0..t4; alınamamış damga None (MR-21)
    status: str


@dataclass
class TelStats:
    scenario: str
    values: dict[str, int]


@dataclass
class Counters:
    kind: str  # "CNI" veya "CNT"
    values: dict[str, int]


@dataclass
class End:
    scenario: str
    n: int


@dataclass
class Ack:
    args: list[str]


@dataclass
class Err:
    reason: str
    command: str


@dataclass
class Boot:
    info: str


@dataclass
class Unknown:
    text: str


@dataclass
class Parsed:
    """Bir satırın ayrıştırma sonucu."""
    msg: object
    frame_error: bool = False     # TEL/BTN 64 bayt değil (UI-04)
    raw: str = field(default="", repr=False)


# ------------------------------------------------------------------- ayrıştırma

def _ints(parts: list[str]) -> list[int]:
    return [int(p) for p in parts]


def parse_line(raw: bytes) -> Parsed:
    """LF ile biten ham bir satırı ayrıştırır. Hata fırlatmaz; bilinmeyeni ``Unknown`` döner."""
    text = raw.decode("ascii", errors="replace").rstrip("\r\n")
    parts = text.rstrip(" ").split(",")
    head = parts[0]

    try:
        if head == "TEL":
            return Parsed(Tel(int(parts[1]), parts[2], int(parts[3])),
                          frame_error=len(raw) != MSG_LEN, raw=text)
        if head == "BTN":
            return Parsed(Btn(int(parts[1]), parts[2]), frame_error=len(raw) != MSG_LEN, raw=text)
        if head == "STA":
            return Parsed(Status(parts[1], parts[2], int(parts[3]), int(parts[4]), int(parts[5])), raw=text)
        if head == "REC":
            t = [int(x) if x else None for x in parts[3:8]]
            return Parsed(Rec(parts[1], int(parts[2]), t, parts[8]), raw=text)
        if head == "TST":
            return Parsed(TelStats(parts[1], dict(zip(TST_FIELDS, _ints(parts[2:])))), raw=text)
        if head == "CNI":
            return Parsed(Counters("CNI", dict(zip(CNI_FIELDS, _ints(parts[1:])))), raw=text)
        if head == "CNT":
            return Parsed(Counters("CNT", dict(zip(CNT_FIELDS, _ints(parts[1:])))), raw=text)
        if head == "END":
            return Parsed(End(parts[1], int(parts[2])), raw=text)
        if head == "ACK":
            return Parsed(Ack(parts[1:]), raw=text)
        if head == "ERR":
            return Parsed(Err(parts[1], ",".join(parts[2:])), raw=text)
        if head == "BOOT":
            return Parsed(Boot(",".join(parts[1:])), raw=text)
    except (IndexError, ValueError):
        pass
    return Parsed(Unknown(text), raw=text)


# ------------------------------------------------------------------ komutlar

def cmd_scen(scenario: str) -> bytes:
    assert scenario in SCENARIOS
    return f"CMD,SCEN,{scenario}\n".encode()


def cmd_start(count: int) -> bytes:
    return f"CMD,START,{int(count)}\n".encode()


def cmd_stop() -> bytes:
    return b"CMD,STOP\n"


def cmd_dump() -> bytes:
    return b"CMD,DUMP\n"


def cmd_stat() -> bytes:
    return b"CMD,STAT\n"


# ------------------------------------------------------------------ hesaplar

def stage_us(t: list[Optional[int]], a: int, b: int) -> Optional[int]:
    """t[b] − t[a] [µs], 32-bit sarma dahil (FR-68). Damgalardan biri yoksa None."""
    if t[a] is None or t[b] is None:
        return None
    return (t[b] - t[a]) & 0xFFFFFFFF
