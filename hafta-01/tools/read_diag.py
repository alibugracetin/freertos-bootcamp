"""Kart çalışırken tanı yapılarını SWD üzerinden okur (işlemciyi durdurmadan).

UART'a hiç trafik eklemez; bu yüzden ölçüm sırasında da güvenle kullanılabilir.
Sembol adreslerini ELF'ten `arm-none-eabi-nm` ile bulur, belleği
`STM32_Programmer_CLI -r32` ile okur.

Kullanım:
    python read_diag.py [--elf yol/firmware.elf]

Araç yolları varsayılan olarak STM32CubeIDE 2.2.0 kurulumundan alınır;
farklıysa CUBE_NM ve CUBE_PROGRAMMER ortam değişkenleriyle verilebilir.

ÖNEMLİ: Aşağıdaki alan listeleri `Core/Inc/app_diag.h` içindeki yapılarla
**aynı sırada** olmalıdır. Yapıya alan eklenirse buraya da eklenmelidir.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

PLUGINS = Path(r"C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins")
NM = os.environ.get("CUBE_NM") or str(
    PLUGINS / "com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740"
    / "tools" / "bin" / "arm-none-eabi-nm.exe")
PROGRAMMER = os.environ.get("CUBE_PROGRAMMER") or str(
    PLUGINS / "com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304"
    / "tools" / "bin" / "STM32_Programmer_CLI.exe")

DEFAULT_ELF = Path(__file__).resolve().parent.parent / "firmware" / "build" / "Debug" / "firmware.elf"

# app_diag.h ile aynı sırada.
STRUCTS: dict[str, list[str]] = {
    "g_diag": [
        "magic", "selfcheck", "selfcheck_opened", "selfcheck_rejected", "task_count",
        "heap_free", "heap_min_free", "btn_wakeups", "last_event_id", "last_t0",
        "last_t1", "last_t1_minus_t0", "max_t1_minus_t0", "fault", "btn_stack_hwm",
    ],
    "g_cnt_isr": ["accepted", "debounce_rej", "btnq_drop", "rec_ovf", "rec_mismatch", "unarmed_rej"],
    "g_cnt_task": ["txq_drop_tel", "txq_drop_btn", "rec_mismatch", "uart_err", "uart_timeout"],
}
DIAG_MAGIC = 0xD1A60001


def symbol_table(elf: Path) -> dict[str, tuple[int, int]]:
    """ELF'teki sembollerin (adres, boyut) tablosu."""
    out = subprocess.run([NM, "-S", str(elf)], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            table[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    return table


def read_words(addr: int, size: int) -> list[int]:
    """Karttan `size` bayt okur; işlemci durdurulmaz (HOTPLUG)."""
    out = subprocess.run(
        [PROGRAMMER, "-c", "port=SWD", "mode=HOTPLUG", "-r32", f"0x{addr:08X}", str(size)],
        capture_output=True, text=True).stdout
    words = []
    for line in out.splitlines():
        m = re.match(r"\s*0x[0-9A-Fa-f]+\s*:\s*(.*)", line)
        if m:
            words += [int(w, 16) for w in m.group(1).split()]
    if not words:
        sys.exit("Okuma başarısız. ST-LINK bağlı mı? Başka bir program (CubeIDE hata ayıklayıcı) kartı tutuyor olabilir.\n" + out[-800:])
    return words


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    args = ap.parse_args()

    syms = symbol_table(args.elf)
    for name, fields in STRUCTS.items():
        if name not in syms:
            sys.exit(f"{name} sembolü ELF'te yok: {args.elf}")
        addr, size = syms[name]
        if size != 4 * len(fields):
            sys.exit(f"{name}: ELF'te {size} bayt, betikte {4 * len(fields)} — app_diag.h ile senkron değil.")
        values = read_words(addr, size)
        print(f"\n[{name}]  @0x{addr:08X}")
        for field, v in zip(fields, values):
            print(f"  {field:<20} {v:>12}   0x{v:08X}")

        if name == "g_diag" and values[0] != DIAG_MAGIC:
            print("  !! magic uyuşmuyor: karttaki firmware bu ELF değil ya da henüz açılmadı.")


if __name__ == "__main__":
    main()
