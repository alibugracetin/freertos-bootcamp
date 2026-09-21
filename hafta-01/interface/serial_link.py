"""Seri port okuma iş parçacığı: LF ile çerçeveleme, ham satırları kuyruğa koyma.

tkinter tek iş parçacıklıdır; bloklayan ``read()`` arayüzü dondurur. Bu yüzden
okuma ayrı bir iş parçacığında yapılır ve satırlar ``queue.Queue`` ile ana
iş parçacığına aktarılır. Widget'lara yalnızca ana iş parçacığı dokunur.

Seri okuma parçalı gelir (USB paketleri); çerçeve sınırı yalnızca LF'dir (UI-02).
"""
from __future__ import annotations

import queue
import threading
from typing import Optional

import serial
import serial.tools.list_ports

BAUD = 115200


def list_ports() -> list[tuple[str, str]]:
    """(port, açıklama) listesi; FTDI dönüştürücüsü önce."""
    ports = [(p.device, p.description) for p in serial.tools.list_ports.comports()]
    ports.sort(key=lambda p: (0 if "USB Serial" in p[1] or "FTDI" in p[1] else 1, p[0]))
    return ports


class SerialLink:
    """Bir seri portu açar, arka planda okur; satırları ``lines`` kuyruğuna koyar.

    Kuyruğa giden öğeler: ``("line", bytes)`` tam satır (LF dahil) veya
    ``("error", str)`` bağlantı hatası.
    """

    def __init__(self) -> None:
        self.lines: "queue.Queue[tuple[str, object]]" = queue.Queue()
        self._ser: Optional[serial.Serial] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def open(self, port: str) -> None:
        self.close()
        self._ser = serial.Serial(port, BAUD, timeout=0.05)
        self._ser.reset_input_buffer()
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader, name="serial-reader", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None
        if self._ser is not None:
            try:
                self._ser.close()
            finally:
                self._ser = None

    def write(self, data: bytes) -> None:
        if self._ser is None:
            raise RuntimeError("port kapalı")
        self._ser.write(data)

    def _reader(self) -> None:
        buf = b""
        ser = self._ser
        while not self._stop.is_set():
            try:
                chunk = ser.read(512)
            except (serial.SerialException, OSError) as exc:
                self.lines.put(("error", str(exc)))
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.lines.put(("line", line + b"\n"))
