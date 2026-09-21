"""UART Monitor'ü başlatır.

Kullanım (hafta-01/interface klasöründen veya herhangi bir yerden):
    python main.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gui import App  # noqa: E402

if __name__ == "__main__":
    App().mainloop()
