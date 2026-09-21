"""UART Monitor — deney kontrolü ve canlı izleme arayüzü (tkinter).

Arayüz ölçümü **göstermek** içindir, ölçüme karışmaz (UI-11):
- Tüm zamanlar kartın TIM2 sayacından gelir; PC saati yalnızca ekran yenileme içindir.
- Telemetri hızı, TEL mesajlarındaki kart zaman damgalarından hesaplanır. USB
  satırları paketler halinde teslim ettiği için PC'nin varış zamanları yanıltıcıdır.
- Ölçüm sürerken arayüz komut göndermez (UI-13); tek istisna kullanıcının açıkça
  istediği STOP'tur.
"""
from __future__ import annotations

import queue
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from tkinter import messagebox, ttk
from tkinter.scrolledtext import ScrolledText
from typing import Optional

import protocol as P
from recorder import MIN_OFFICIAL, DumpCollector, DumpResult
from serial_link import SerialLink, list_ports

POLL_MS = 50
CMD_TIMEOUT_S = 2.0
LINE_BAUD = 115_385            # gerçek baud (BRR = 22.75 @ 42 MHz), tasarım §12.1
STATE_COLOR = {
    "IDLE": "#2e9e44", "ARMED": "#2e9e44", "WARMUP": "#e8891c",
    "MEASURING": "#1f6fd1", "DRAINING": "#d23b3b", "DONE": "#d23b3b",
}
STATE_TEXT = {
    "IDLE": "Boşta", "ARMED": "Senaryo hazır", "WARMUP": "Isınma (5 s)",
    "MEASURING": "ÖLÇÜM — butona basın", "DRAINING": "Bitiriliyor…", "DONE": "Tamamlandı",
}


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("UART Monitor — Hafta 1 · Yük altında buton yanıtı")
        self.geometry("980x760")
        self.minsize(820, 640)

        self.link = SerialLink()

        # Kartın bildirdiği durum (STA satırlarından)
        self.status: Optional[P.Status] = None
        # Telemetri: son TEL'lerin kart zaman damgaları
        self.tel_times: deque[int] = deque(maxlen=101)
        self.tel_scen = ""
        self.tel_count = 0
        self.frame_errors = 0
        self.unknown_lines = 0
        # Komut sıralayıcı: (komut, gönderimden önce bekleme ms)
        self.steps: deque[tuple[bytes, int]] = deque()
        self.awaiting: Optional[bytes] = None
        self.await_deadline = 0.0
        # Döküm
        self.collector: Optional[DumpCollector] = None
        self.btn_flash_until = 0.0

        self._build_ui()
        self._refresh_ports()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(POLL_MS, self._poll)

    # ================================================================== arayüz

    def _build_ui(self) -> None:
        pad = {"padx": 8, "pady": 4}
        style = ttk.Style(self)
        style.configure("Big.TLabel", font=("Segoe UI", 16, "bold"))
        style.configure("Mid.TLabel", font=("Segoe UI", 11))
        style.configure("Hint.TLabel", foreground="#666")

        # ---- bağlantı
        top = ttk.Frame(self)
        top.pack(fill="x", **pad)
        ttk.Label(top, text="Seri port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=38, state="readonly")
        self.port_box.pack(side="left", padx=4)
        ttk.Button(top, text="↻", width=3, command=self._refresh_ports).pack(side="left")
        self.conn_btn = ttk.Button(top, text="Bağlan", command=self._toggle_connect)
        self.conn_btn.pack(side="left", padx=6)
        self.conn_lbl = ttk.Label(top, text="● bağlı değil", foreground="#999")
        self.conn_lbl.pack(side="left", padx=6)

        mid = ttk.Frame(self)
        mid.pack(fill="x", **pad)

        # ---- senaryo
        left = ttk.LabelFrame(mid, text="Senaryo")
        left.pack(side="left", fill="y", padx=(0, 8))
        self.scen_var = tk.StringVar(value="S0")
        for sc, desc in P.SCENARIOS.items():
            ttk.Radiobutton(left, text=f"{sc}  {desc}", value=sc, variable=self.scen_var).pack(anchor="w", padx=8, pady=1)
        row = ttk.Frame(left)
        row.pack(fill="x", padx=8, pady=(8, 2))
        ttk.Label(row, text="Hedef olay:").pack(side="left")
        self.count_var = tk.IntVar(value=MIN_OFFICIAL)
        ttk.Spinbox(row, from_=1, to=64, width=5, textvariable=self.count_var).pack(side="left", padx=4)
        ttk.Label(left, text=f"< {MIN_OFFICIAL} ise deneme/ klasörüne kaydedilir", style="Hint.TLabel").pack(anchor="w", padx=8)
        btns = ttk.Frame(left)
        btns.pack(fill="x", padx=8, pady=8)
        self.start_btn = ttk.Button(btns, text="▶ Başlat", command=self._on_start)
        self.start_btn.grid(row=0, column=0, sticky="ew", padx=2, pady=2)
        self.stop_btn = ttk.Button(btns, text="■ Durdur", command=self._on_stop)
        self.stop_btn.grid(row=0, column=1, sticky="ew", padx=2, pady=2)
        self.dump_btn = ttk.Button(btns, text="Kayıtları al", command=self._on_dump)
        self.dump_btn.grid(row=1, column=0, sticky="ew", padx=2, pady=2)
        self.stat_btn = ttk.Button(btns, text="Durum sor", command=lambda: self._send(P.cmd_stat()))
        self.stat_btn.grid(row=1, column=1, sticky="ew", padx=2, pady=2)
        btns.columnconfigure((0, 1), weight=1)

        # ---- durum
        right = ttk.LabelFrame(mid, text="Kart durumu")
        right.pack(side="left", fill="both", expand=True)
        srow = ttk.Frame(right)
        srow.pack(fill="x", padx=8, pady=(8, 2))
        self.led = tk.Canvas(srow, width=26, height=26, highlightthickness=0)
        self.led_dot = self.led.create_oval(3, 3, 23, 23, fill="#bbb", outline="")
        self.led.pack(side="left")
        self.state_lbl = ttk.Label(srow, text="—", style="Big.TLabel")
        self.state_lbl.pack(side="left", padx=8)
        self.scen_lbl = ttk.Label(right, text="Senaryo: —   Olay: — / —", style="Mid.TLabel")
        self.scen_lbl.pack(anchor="w", padx=8)

        tel = ttk.Frame(right)
        tel.pack(fill="x", padx=8, pady=(10, 2))
        self.tel_lbl = ttk.Label(tel, text="Telemetri: —", style="Mid.TLabel")
        self.tel_lbl.pack(anchor="w")
        self.line_bar = ttk.Progressbar(tel, length=320, maximum=100)
        self.line_bar.pack(anchor="w", pady=2)
        self.line_lbl = ttk.Label(tel, text="UART hat doluluğu (telemetri): —", style="Hint.TLabel")
        self.line_lbl.pack(anchor="w")
        self.frame_lbl = ttk.Label(right, text="TEL: 0   çerçeve hatası: 0   tanınmayan: 0", style="Hint.TLabel")
        self.frame_lbl.pack(anchor="w", padx=8, pady=(6, 0))

        self.btn_lbl = tk.Label(right, text=" ", font=("Segoe UI", 15, "bold"), height=2, bg=self.cget("bg"))
        self.btn_lbl.pack(fill="x", padx=8, pady=8)

        # ---- alt bölüm: sekmeler
        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True, **pad)
        res = ttk.Frame(self.tabs)
        self.plot_tab = ttk.Frame(self.tabs)
        logf = ttk.Frame(self.tabs)
        self.tabs.add(res, text="  Ölçüm  ")
        self.tabs.add(self.plot_tab, text="  Grafikler  ")
        self.tabs.add(logf, text="  Günlük  ")
        cols = ("id", "t1-t0", "t2-t1", "t3-t2", "t4-t3", "R", "status")
        heads = ("olay", "t₁−t₀ µs", "t₂−t₁ µs", "t₃−t₂ µs", "t₄−t₃ µs", "R ms", "durum")
        self.tree = ttk.Treeview(res, columns=cols, show="headings", height=8)
        for c, h in zip(cols, heads):
            self.tree.heading(c, text=h)
            self.tree.column(c, width=100 if c != "id" else 60, anchor="e")
        self.tree.column("status", anchor="center")
        self.tree.tag_configure("late", foreground="#d23b3b")
        self.tree.tag_configure("bad", foreground="#999")
        self.summary_lbl = ttk.Label(res, text="Son ölçümün kayıtları burada görünür.", style="Mid.TLabel")
        self.summary_lbl.pack(side="bottom", anchor="w", padx=4, pady=4)
        sb = ttk.Scrollbar(res, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="left", fill="y")

        # ---- günlük
        ttk.Label(logf, text="TEL satırları sayılır ama gösterilmez.", style="Hint.TLabel").pack(anchor="w")
        self.log = ScrolledText(logf, height=8, font=("Consolas", 9), state="disabled")
        self.log.pack(fill="both", expand=True)

        self._build_plot_tab()
        self._update_controls()

    # ================================================================ grafikler

    def _build_plot_tab(self) -> None:
        """Grafik sekmesi: CSV'den çizim (UI-20…28). Çizim kodu plots.py'dedir."""
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

        self._FigureCanvasTkAgg = FigureCanvasTkAgg
        bar = ttk.Frame(self.plot_tab)
        bar.pack(fill="x", pady=(4, 2))
        ttk.Label(bar, text="Kaynak:").pack(side="left")
        self.src_var = tk.StringVar(value="deneme")
        ttk.Radiobutton(bar, text="Resmî (measurements/)", value="resmi", variable=self.src_var,
                        command=self._refresh_plots).pack(side="left", padx=4)
        ttk.Radiobutton(bar, text="Deneme (measurements/deneme/)", value="deneme", variable=self.src_var,
                        command=self._refresh_plots).pack(side="left", padx=4)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        self.scen_checks: dict[str, tk.BooleanVar] = {}
        self.scen_cb: dict[str, ttk.Checkbutton] = {}
        for sc in P.SCENARIOS:
            v = tk.BooleanVar(value=True)
            cb = ttk.Checkbutton(bar, text=sc, variable=v, command=self._refresh_plots)
            cb.pack(side="left")
            self.scen_checks[sc], self.scen_cb[sc] = v, cb
        ttk.Button(bar, text="PNG kaydet", command=self._save_pngs).pack(side="right", padx=2)
        ttk.Button(bar, text="↻ Yenile", command=self._refresh_plots).pack(side="right", padx=2)

        self.plot_tabs = ttk.Notebook(self.plot_tab)
        self.plot_tabs.pack(fill="both", expand=True)
        self.canvases: dict[str, object] = {}
        for key, title in (("r", "Olay → R (deadline)"), ("stages", "Aşama dağılımı"), ("table", "Tablo")):
            frame = ttk.Frame(self.plot_tabs)
            self.plot_tabs.add(frame, text=title)
            self.canvases[key] = frame

        # Tablo görünümü: grafikteki her değerin sayısı (erişilebilirlik; renk tek başına taşımasın)
        cols = ("sc", "n", "s1", "s2", "s3", "s4", "R")
        heads = ("senaryo", "n (ok)", "t₁−t₀ µs", "t₂−t₁ µs", "t₃−t₂ µs", "t₄−t₃ µs", "R ort. ms")
        self.mean_tree = ttk.Treeview(self.canvases["table"], columns=cols, show="headings", height=7)
        for c, h in zip(cols, heads):
            self.mean_tree.heading(c, text=h)
            self.mean_tree.column(c, width=110, anchor="e")
        self.mean_tree.pack(fill="both", expand=True, padx=4, pady=4)
        ttk.Label(self.canvases["table"], text="Ortalamalar yalnızca status = ok olayları kapsar.",
                  style="Hint.TLabel").pack(anchor="w", padx=4)

        self.plot_note = ttk.Label(self.plot_tab, text="", style="Hint.TLabel")
        self.plot_note.pack(anchor="w")
        self.after(200, self._refresh_plots)

    def _plot_folder(self):
        from recorder import MEASUREMENTS
        return MEASUREMENTS / "deneme" if self.src_var.get() == "deneme" else MEASUREMENTS

    def _selected_csvs(self) -> list:
        import plots
        folder = self._plot_folder()
        found = {p.stem: p for p in plots.available(folder)}
        for sc, cb in self.scen_cb.items():
            cb.configure(state="normal" if sc in found else "disabled")
        return [found[sc] for sc in P.SCENARIOS if sc in found and self.scen_checks[sc].get()]

    def _refresh_plots(self) -> None:
        """Seçili CSV'lerden grafikleri yeniden çizer. Girdi daima dosyadır (UI-26)."""
        import plots

        paths = self._selected_csvs()
        note = "deneme" if self.src_var.get() == "deneme" else ""
        for key, fn in (("r", plots.plot_response_times), ("stages", plots.plot_stage_breakdown)):
            frame = self.canvases[key]
            for child in frame.winfo_children():
                child.destroy()
            fig = fn(paths, note)
            canvas = self._FigureCanvasTkAgg(fig, master=frame)
            canvas.draw()
            canvas.get_tk_widget().pack(fill="both", expand=True)
            tip = ttk.Label(frame, text="", style="Hint.TLabel")
            tip.pack(anchor="w", padx=4)
            canvas.mpl_connect("motion_notify_event",
                               lambda ev, f=fig, t=tip: t.configure(text=plots.figure_hover_text(f, ev) or ""))

        self.mean_tree.delete(*self.mean_tree.get_children())
        for sc, n, means in plots.stage_means(paths):
            self.mean_tree.insert("", "end", values=(sc, n, *[f"{m:,.1f}".replace(",", " ") for m in means],
                                                     f"{sum(means) / 1000:.3f}"))
        folder = self._plot_folder()
        self.plot_note.configure(
            text=f"Kaynak: {folder}  ·  {len(paths)} senaryo" if paths else f"{folder} içinde çizilecek CSV yok.")

    def _save_pngs(self) -> None:
        import plots

        paths = self._selected_csvs()
        if not paths:
            messagebox.showinfo("PNG", "Kaydedilecek grafik yok.")
            return
        out_dir = Path(__file__).resolve().parent.parent / "analysis" / "plots"
        out_dir.mkdir(parents=True, exist_ok=True)
        suffix = "_deneme" if self.src_var.get() == "deneme" else ""
        written = []
        for name, fn in (("r_per_event", plots.plot_response_times),
                         ("stage_breakdown", plots.plot_stage_breakdown)):
            out = out_dir / f"{name}{suffix}.png"
            fn(paths, suffix.strip("_")).savefig(out, dpi=150, facecolor=plots.SURFACE)
            written.append(out.name)
        self._log(f"--- PNG kaydedildi: {out_dir} ({', '.join(written)})")
        messagebox.showinfo("PNG", f"Kaydedildi:\n{out_dir}\n\n" + "\n".join(written))

    def _log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 1500:
            self.log.delete("1.0", "500.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    # ================================================================ bağlantı

    def _refresh_ports(self) -> None:
        ports = list_ports()
        self.port_box["values"] = [f"{d} — {desc}" for d, desc in ports]
        if ports and not self.port_var.get():
            self.port_box.current(0)

    def _toggle_connect(self) -> None:
        if self.link.is_open:
            self.link.close()
            self._log("--- bağlantı kapatıldı")
        else:
            sel = self.port_var.get()
            if not sel:
                messagebox.showwarning("Port", "Önce bir seri port seçin.")
                return
            port = sel.split(" — ")[0]
            try:
                self.link.open(port)
            except Exception as exc:  # noqa: BLE001 — kullanıcıya göster
                messagebox.showerror("Bağlantı", f"{port} açılamadı:\n{exc}")
                return
            self._log(f"--- {port} açıldı (115200 8N1)")
            self._send(P.cmd_stat())
        self._update_controls()

    def _on_close(self) -> None:
        self.link.close()
        self.destroy()

    # ================================================================ komutlar

    def _send(self, cmd: bytes) -> None:
        if not self.link.is_open:
            return
        try:
            self.link.write(cmd)
            self._log(">>> " + cmd.decode().strip())
        except Exception as exc:  # noqa: BLE001
            self._log(f"!!! yazma hatası: {exc}")

    def _run_steps(self, steps: list[tuple[bytes, int]]) -> None:
        """ACK bekleyen komutları sırayla gönderir."""
        self.steps = deque(steps)
        self.awaiting = None
        self._send_next_step()

    def _send_next_step(self) -> None:
        if not self.steps:
            self.awaiting = None
            return
        cmd, delay = self.steps[0]

        def go() -> None:
            self.awaiting = cmd
            self.await_deadline = time.monotonic() + CMD_TIMEOUT_S
            self._send(cmd)

        self.after(delay, go) if delay else go()

    def _on_start(self) -> None:
        sc = self.scen_var.get()
        try:
            n = max(1, min(64, int(self.count_var.get())))
        except (tk.TclError, ValueError):
            n = MIN_OFFICIAL
        running = self.status and self.status.state in ("WARMUP", "MEASURING", "DRAINING")
        if self.status and self.status.state == "MEASURING":
            if not messagebox.askyesno("Ölçüm sürüyor", "Koşan ölçüm iptal edilecek. Devam edilsin mi?"):
                return
        steps = []
        if running:
            steps.append((P.cmd_stop(), 0))
            steps.append((P.cmd_scen(sc), 250))    # telemetrinin durmasını bekle
        else:
            steps.append((P.cmd_scen(sc), 0))
        steps.append((P.cmd_start(n), 0))
        self._run_steps(steps)

    def _on_stop(self) -> None:
        self.steps.clear()
        self.awaiting = None
        self._send(P.cmd_stop())

    def _on_dump(self) -> None:
        self._begin_dump()

    def _begin_dump(self) -> None:
        st = self.status
        if st is None:
            return
        self.collector = DumpCollector(st.scenario, expected_events=st.events, target=st.target)
        self.tree.delete(*self.tree.get_children())
        self.summary_lbl.configure(text="Kayıtlar alınıyor…")
        self._send(P.cmd_dump())

    # ================================================================ döngü

    def _poll(self) -> None:
        for _ in range(400):                       # tur başına üst sınır; arayüz donmasın
            try:
                kind, payload = self.link.lines.get_nowait()
            except queue.Empty:
                break
            if kind == "error":
                self._log(f"!!! bağlantı hatası: {payload}")
                self.link.close()
                self._update_controls()
                break
            self._handle(P.parse_line(payload))

        if self.awaiting and time.monotonic() > self.await_deadline:
            self._log(f"!!! yanıt gelmedi: {self.awaiting.decode().strip()}")
            self.steps.clear()
            self.awaiting = None

        if self.btn_flash_until and time.monotonic() > self.btn_flash_until:
            self.btn_lbl.configure(bg=self.cget("bg"))
            self.btn_flash_until = 0.0

        self._update_telemetry()
        self.after(POLL_MS, self._poll)

    def _handle(self, p: P.Parsed) -> None:
        m = p.msg
        if p.frame_error:
            self.frame_errors += 1
            self._log(f"!!! 64 bayt değil: {p.raw!r}")

        if isinstance(m, P.Tel):
            if m.scenario != self.tel_scen:
                self.tel_times.clear()
                self.tel_scen = m.scenario
            self.tel_times.append(m.t_us)
            self.tel_count += 1
            return

        if isinstance(m, P.Unknown):
            self.unknown_lines += 1
            self._log(f"??? {m.text}")
            return

        if not isinstance(m, (P.Rec,)):
            self._log("    " + p.raw)

        if isinstance(m, P.Status):
            self._on_status(m)
        elif isinstance(m, P.Btn):
            self.btn_lbl.configure(text=f"Butona basıldı · Olay {m.event_id}", bg="#cfe3ff")
            self.btn_flash_until = time.monotonic() + 0.6
        elif isinstance(m, P.Ack):
            if self.awaiting is not None and self.steps:
                self.steps.popleft()
                self.awaiting = None
                self._send_next_step()
        elif isinstance(m, P.Err):
            if m.reason == "busy" and self.awaiting is not None:
                cmd = self.awaiting                # telemetri henüz durmadı: tekrar dene
                self.awaiting = None
                self.after(150, lambda: self._retry(cmd))
            else:
                self.steps.clear()
                self.awaiting = None
        elif isinstance(m, P.Boot):
            self.status = None
        elif isinstance(m, P.Rec):
            self._add_rec_row(m)

        if self.collector is not None and isinstance(m, (P.Rec, P.TelStats, P.Counters, P.End)):
            res = self.collector.feed(m)
            if res is not None:
                self.collector = None
                self._on_dump_done(res)

    def _retry(self, cmd: bytes) -> None:
        self.awaiting = cmd
        self.await_deadline = time.monotonic() + CMD_TIMEOUT_S
        self._send(cmd)

    def _on_status(self, st: P.Status) -> None:
        prev = self.status.state if self.status else None
        self.status = st
        self.led.itemconfigure(self.led_dot, fill=STATE_COLOR.get(st.state, "#bbb"))
        self.state_lbl.configure(text=STATE_TEXT.get(st.state, st.state))
        self.scen_lbl.configure(text=f"Senaryo: {st.scenario} ({P.SCENARIOS.get(st.scenario, '')})"
                                     f"   Olay: {st.events} / {st.target}")
        if st.state == "DONE" and prev != "DONE" and self.collector is None:
            self._begin_dump()                      # UI-08: otomatik döküm
        self._update_controls()

    # ================================================================ sonuçlar

    def _add_rec_row(self, r: P.Rec) -> None:
        d = [P.stage_us(r.t, a, a + 1) for a in range(4)]
        R = P.stage_us(r.t, 0, 4)
        fmt = lambda v: "" if v is None else f"{v:,}".replace(",", " ")
        tag = "bad" if r.status != "ok" else ("late" if R is not None and R > 20_000 else "")
        self.tree.insert("", "end", values=(r.event_id, *[fmt(v) for v in d],
                                            "" if R is None else f"{R / 1000:.3f}", r.status), tags=(tag,))

    def _on_dump_done(self, res: DumpResult) -> None:
        rows = [self.tree.item(i, "values") for i in self.tree.get_children()]
        R = [float(v[5]) for v in rows if v[5] and v[6] == "ok"]
        missing = sum(1 for v in rows if v[6] != "ok")
        over = sum(1 for r in R if r > 20.0)
        if R:
            self.summary_lbl.configure(
                text=f"{res.scenario}: n = {len(R)} ok · R min / ort / max = "
                     f"{min(R):.2f} / {sum(R) / len(R):.2f} / {max(R):.2f} ms · "
                     f"20 ms aşımı: {over} · eksik: {missing}")
        self._log(f"--- kaydedildi: {res.csv_path}")
        for w in res.warnings:
            self._log(f"!!! {w}")
        # Yeni CSV'yi hemen çiz: kaynağı yazılan klasöre çevir (UI-08 → UI-20)
        self.src_var.set("deneme" if res.csv_path.parent.name == "deneme" else "resmi")
        self._refresh_plots()
        serious = [w for w in res.warnings if "kayıp" in w or "Beklenen" in w]
        if serious:
            messagebox.showwarning("Döküm uyarısı", "\n".join(serious))

    # ================================================================ yardımcılar

    def _update_telemetry(self) -> None:
        ts = list(self.tel_times)
        running = self.status and self.status.state in ("WARMUP", "MEASURING")
        if len(ts) >= 2 and running:
            span = (ts[-1] - ts[0]) & 0xFFFFFFFF
            hz = (len(ts) - 1) * 1e6 / span if span else 0.0
            load = min(100.0, P.MSG_LEN * 10 * hz / LINE_BAUD * 100)
            self.tel_lbl.configure(text=f"Telemetri: {hz:6.1f} Hz   (kart saatinden, son {len(ts) - 1} aralık)")
            self.line_bar["value"] = load
            self.line_lbl.configure(text=f"UART hat doluluğu (telemetri): %{load:.1f}")
        else:
            self.tel_lbl.configure(text="Telemetri: kapalı")
            self.line_bar["value"] = 0
            self.line_lbl.configure(text="UART hat doluluğu (telemetri): %0")
        self.frame_lbl.configure(text=f"TEL: {self.tel_count}   çerçeve hatası: {self.frame_errors}"
                                      f"   tanınmayan: {self.unknown_lines}")

    def _update_controls(self) -> None:
        connected = self.link.is_open
        self.conn_btn.configure(text="Bağlantıyı kes" if connected else "Bağlan")
        self.conn_lbl.configure(text="● bağlı" if connected else "● bağlı değil",
                                foreground="#2e9e44" if connected else "#999")
        st = self.status.state if self.status else None
        measuring = st == "MEASURING"
        # UI-13: ölçüm sürerken yalnızca Durdur (ve bilinçli iptal için Başlat) açık
        self.start_btn.configure(state="normal" if connected else "disabled")
        self.stop_btn.configure(state="normal" if connected else "disabled")
        self.dump_btn.configure(state="normal" if connected and st == "DONE" else "disabled")
        self.stat_btn.configure(state="normal" if connected and not measuring else "disabled")
