# 03 — Görev Listesi (Tasks)

**Girdi:** `01-requirements.md`, `02-design.md`
**Bugün:** 20 Eylül 2026, Pazar akşamı
**Teslim:** 27 Eylül 2026 Pazar **11.00**

```
Paz 20   Pzt 21   Sal 22   Çar 23   Per 24   Cum 25   Cmt 26   Paz 27
 ───      ████     ████     ████     ████     ████     ████     ██ 11.00
 din-     ortam    çekir-   çekir-   deney    ÖLÇÜM    sunum    teslim
 lenme    donanım  dek I    dek II   kontrol  🔴       tam gün
                            🎯                          
```

**Gerçek çalışma süresi: 6 gün.** Pazar sabahı yalnızca teslim penceresidir, iş planlanmaz.

> **Varsayım:** Hafta içi akşamları ~3 saat, Cumartesi tam gün (~8 saat). Toplam ≈23 saat.
> Müsaitliğin bundan farklıysa söyle, planı ona göre ölçeklerim.

---

## Planın mantığı

Önceki plandan **üç yapısal değişiklik** yaptım:

1. **Ölçüm günü Cuma'ya çekildi, Cumartesi tamamen sunuma ayrıldı.** Veri Cuma akşamı cebinde olacak. Cuma aksarsa Cumartesi yedek ölçüm günü olur — ama o zaman sunum sıkışır.
2. **GUI, ölçümün önüne geçmiyor.** Perşembe yalnızca *minimal bir Python betiği* yazılıyor (komut gönder, `REC` topla, CSV yaz). Görsel arayüz Cumartesi'ye kaldı. Ölçüm almak için güzel bir arayüze ihtiyaç yok; **veriye ihtiyaç var.**
3. **Doxygen ayrı bir gün değil.** Yorumlar kod yazılırken yazılıyor. Cumartesi yalnızca `Doxyfile` + üretim + uyarı temizliği kaldı.

**Değişmeyen tek kural:** Süre sıkışırsa grafik sadeleşir, video kısalır, GUI çirkin kalır — **ölçüm verisi asla kısılmaz.**

**İlerleme:** 2 / 26

---

# 🛌 Pazar 20 — Dinlenme

Bugün iş yok. Yalnızca istersen:

- [ ] FT232RL'in **VCCIO jumper'ını 3.3 V**'a al (30 saniye, yarın zaman kazandırır)
- [ ] Kartı ve kabloları masaya çıkar

---

# Pazartesi 21 — Ortam ve donanım
### 🏁 Gün hedefi: Kart PC ile konuşuyor, buton ve timer doğrulandı

### T-01 · Git kurulumu ve depo ✅
- [x] Git 2.55 kuruldu (winget) — ayrıca Python 3.12, Doxygen 1.18, Graphviz 16.1
- [x] `git init -b main` + `.gitignore` (derleme çıktıları, `build/`, Python önbelleği)
- [x] `ders-materyali/` **depo dışında** — eğitmen materyali yeniden yayınlanmaz
- [x] İlk commit: `specs/` → `28f87d2`
- [x] GitHub: [alibugracetin/freertos-bootcamp](https://github.com/alibugracetin/freertos-bootcamp) (private), push ✓
- **Kanıt:** ✅ Çalışan GitHub linki
- **Gereksinim:** DR-01

### T-02 · CubeMX projesi ✅
- [x] **CubeMX betik modunda** üretildi (`-q`), `.ioc` elle yazıldı → `hafta-01/firmware/firmware.ioc`
- [x] Saat: HSE 8 MHz → PLL (M=8, N=336, P=2, Q=7) → **SYSCLK 168 MHz**, APB1 42 / timer 84 MHz
- [x] **SYS → Timebase Source = TIM6**
- [x] USART2: 115200 8N1, **PA2/PA3**, **DMA1 Stream6 Ch4 (TX)**, global interrupt açık
- [x] TIM2: `PSC = 83`, `ARR = 0xFFFFFFFF`, kesme **kapalı**
- [x] PA0 → EXTI0, yükselen kenar, pull yok, etiket `BTN_USER`
- [x] PD12–PD15 → çıkış, `LED_GREEN / ORANGE / RED / BLUE`
- [x] FreeRTOS CMSIS_V2, tick 1000 Hz, stack overflow kontrolü 2, malloc hook, newlib reentrant, heap 20 KB
- [x] `configMAX_PRIORITIES` = **56** (CubeMX kilitli; ödeve uygun, görevler 1/2/3 ile açılacak)
- [x] `configUSE_TIMERS = 0` — CubeMX zorladığı için USER CODE'da geçersiz kılındı
- [x] `defaultTask` — CubeMX silinmesine izin vermedi, USER CODE'da scheduler öncesi sonlandırılıyor
- [x] NVIC: EXTI0 = 5, USART2 = 5, DMA1_Stream6 = 5 (üretilen kodda doğrulandı)
- [x] Proje formatı **CMake** — CubeIDE servis hatası nedeniyle (tasarım §13.1)
- **Kanıt:** ✅ **0 hata, 0 uyarı**; RAM %19, CCMRAM 0 B, FLASH %2. ELF'te timer daemon sembolü **yok**.
- **Kalan:** `uxTaskGetNumberOfTasks()` = 1 çalışma zamanı kanıtı T-03'te karta yüklenince alınacak.
- **Gereksinim:** FR-01…04, tasarım §2.2, §8, §13

### T-03 · Donanım doğrulama 🔴
- [ ] FT232 kablola: TXD→PA3, RXD→PA2, GND→GND. **VCC bağlama.**
- [ ] Blocking `HAL_UART_Transmit` ile `"hello\n"` → terminalde gör
- [ ] PA0'ı GPIO giriş olarak oku → **aktif seviyeyi doğrula** (HIGH bekliyoruz)
- [ ] 4 LED'i sırayla yak
- **Kanıt:** Terminal çıktısı + butonun aktif seviyesi yazılı not
- **Gereksinim:** HW-01…04
- **⚠️ Buradan geçmeden ilerleme.** Kablolama hatası sonraki her adımı yanıltır.

### T-04 · TIM2 zaman kaynağı
- [ ] `timer_us()` → `TIM2->CNT` doğrudan okuma
- [ ] `HAL_Delay(1000)` etrafında ölç → **1 000 000 ± birkaç yüz µs**
- [ ] `uint32_t` sarma aritmetiğini elle test et
- **Kanıt:** Ölçülen 1 sn değerinin çıktısı
- **Gereksinim:** FR-68, FR-72, FR-73

> **Gün sonu kontrolü:** Terminalde kartın mesajını görüyor musun? Butonun hangi seviyede olduğunu biliyor musun? Timer 1 µs sayıyor mu? Üçü de evet ise yarına hazırsın.

---

# Salı 22 — Ölçüm çekirdeği I
### 🏁 Gün hedefi: 3 görev ayakta, buton olayı göreve ulaşıyor

### T-05 · Veri yapıları ve kayıt defteri
- [ ] `ButtonEvent`, `TxMsg`, `EventRecord`, `RecStatus`
- [ ] `rec_open()`, `rec_stamp()`, `rec_close()`, `rec_reset()`
- [ ] `event_id` doğrulaması — yanlış slota yazma engeli
- [ ] ISR/görev sayaç ayrımı: `g_ovf_isr` / `g_ovf_task`
- [ ] **Doxygen yorumları aynı anda** (`@par Paylaşılan durum`)
- **Kanıt:** 100 sahte olay yaz → slot çakışmasında `ovf` artıyor
- **Gereksinim:** FR-60…62, DOC-06, tasarım §3, §3.1

### T-06 · EXTI0 ISR + debounce
- [ ] t₀ **ilk satırda**, sonra bayrak temizle
- [ ] 30 ms tekrar-kenar filtresi; **ilk olay muaf**
- [ ] `event_id` ata → `rec_open()` → `xQueueSendFromISR`
- [ ] Dönüş değeri kontrolü + drop kaydı
- [ ] **Doxygen** (`@note ISR bağlamı`, `@par Zaman damgası: t₀`)
- **Kanıt:** Hızlı 10 basış → `accepted` 1–2, `debounce_rej` belirgin artar
- **Gereksinim:** FR-10…18, DOC-04, DOC-05

### T-07 · Üç görev + kuyruklar
- [ ] `buttonQ` (8 × `ButtonEvent`), `txQ` (16 × `TxMsg`)
- [ ] `TelemetryTask` (3), `ButtonTask` (2), `UartTxTask` (1)
- [ ] Görevler şimdilik iskelet: blokla + LED yak
- **Kanıt:** `uxTaskGetNumberOfTasks()` = **4** (3 + Idle); öncelikler debugger'da doğru
- **Gereksinim:** FR-01…06

> **Gün sonu kontrolü:** Butona bastığında `ButtonTask` uyanıp LED yakıyor mu? Evet ise ISR→kuyruk→görev zinciri çalışıyor demektir.

---

# Çarşamba 23 — Ölçüm çekirdeği II 🎯
### 🏁 Gün hedefi: Tek bir basışın t₀…t₄'ünü görmek

### T-08 · UartTxTask + DMA + **TC yolu doğrulaması** 🔴
- [ ] `g_tx_buf` global tampon (normal SRAM — CCM değil)
- [ ] `HAL_UART_Transmit_DMA` + `ulTaskNotifyTake` ile TC bekleme
- [ ] `HAL_UART_TxCpltCallback` içinde t₄ + görev uyandırma
- [ ] **HAL kaynağını oku:** `UART_DMATransmitCplt`, `UART_EndTransmit_IT`
- [ ] Doğrula: callback USART **TC bayrağında** mı, DMA tamamlanmasında mı?
- [ ] Bulguyu `docs/code-notes.md` taslağına **hemen yaz**
- **Kanıt:** HAL kaynağından alıntı + `t₄−t₃` ≈ **5.56 ms**
- **Gereksinim:** FR-40…46, risk R-2
- **⚠️ Ödevin en kritik doğrulaması.** Yanlışsa 180 ölçümün tamamı ≈5.56 ms kayar — ve bunu ancak analiz gününde fark edersin.

### T-09 · 64 bayt mesaj formatlama
- [ ] `make_telemetry()`, `make_button_reply()` — boşluk dolgusu + LF
- [ ] 63 bayt taşma → `fmt_err`, sessiz kesme yok
- **Kanıt:** Terminaldeki her satır tam 64 bayt
- **Gereksinim:** FR-50…53

### T-10 · Uçtan uca tek olay 🎯
- [ ] `ButtonTask`: t₁ → mesaj hazırla → t₂ → `txQ`
- [ ] Tek basış → **beş damganın da** dolduğunu doğrula
- [ ] `R = t₄ − t₀` hesapla, geçici olarak UART'tan yazdır
- **Kanıt:** Telemetri kapalıyken bir basışın t₀…t₄ ve R ≈ 6 ms
- **Gereksinim:** FR-30…35
- **🎯 Buraya geldiğinde ödevin kalbi çalışıyor.** Gerisi çoğaltma ve sunum.

> **Gün sonu kontrolü — haftanın dönüm noktası.** Beş damgayı görebiliyorsan plan sağlam. Göremiyorsan **yarın sabah bana yaz**, Perşembe'yi kurtarma gününe çeviririz.

---

# Perşembe 24 — Deney kontrolü + minimal PC
### 🏁 Gün hedefi: Tam bir senaryo koşusu, elinde ilk CSV

### T-11 · Sayaçlar
- [ ] Tasarım §9'daki 11 sayaç
- [ ] `txq_hwm` her `xQueueSend` sonrası
- [ ] Stack high-water mark
- **Kanıt:** `txQ`'yu kasten doldur → `txq_drop_tel` artıyor
- **Gereksinim:** FR-64, FR-65

### T-12 · Komut alma (RX)
- [ ] `HAL_UART_Receive_IT` bayt bayt, LF'de satır tamamlama
- [ ] Tampon taşması → `cmd_overflow`
- [ ] Ayrıştırma **ISR'de değil**, `ButtonTask` housekeeping turunda
- **Kanıt:** Terminalden `CMD,STAT\n` → `CNT,...` yanıtı
- **Gereksinim:** FR-80…84, FR-92

### T-13 · Deney durum makinesi + LED
- [ ] `IDLE → ARMED → WARMUP → MEASURING → DRAINING → DONE`
- [ ] `experiment_tick()` 50 ms timeout yolunda
- [ ] Senaryo değişimi yalnızca `IDLE`'da; sayaç + kayıt sıfırlama
- [ ] 5 sn ısınma; ısınmada buton olayı kayda alınmaz
- [ ] 30 olayda otomatik `DRAINING`
- [ ] LED: yeşil / turuncu / mavi / kırmızı
- **Kanıt:** Tam bir S1 koşusu; LED geçişleri gözlendi, kendi kendine durdu
- **Gereksinim:** FR-85…90

### T-14 · Kayıt dökümü
- [ ] `DRAINING`: telemetri dur → `txQ` boşalt → 1 sn'de kapanmayan `ST_TIMEOUT`
- [ ] `CMD,DUMP` → `REC` satırları + `CNT` özeti
- [ ] Alınamamış damgalar **boş** (0 değil)
- **Kanıt:** 30 `REC` satırı, biçim MR-20/21'e uygun
- **Gereksinim:** FR-66…69, FR-89

### T-15 · Minimal Python betiği (GUI değil)
- [ ] `serial_reader.py` — thread + LF çerçeveleme
- [ ] `protocol.py` — TEL/BTN/REC/CNT ayrıştırma, CMD üretme
- [ ] `recorder.py` — `REC` birikimi → `measurements/Sx.csv`
- [ ] Basit konsol kontrolü: senaryo seç, başlat, döküm al
- **Kanıt:** `S1.csv` açıldı, başlık ve satırlar MR-20'ye uygun
- **Gereksinim:** UI-02…05, UI-08, UI-09, UI-14
- **Not:** Görsel arayüz Cumartesi. Bugünün amacı **veri alabilmek**.

> **Gün sonu kontrolü:** Elinde gerçek bir `S1.csv` var mı? Varsa yarın ölçüm günü. Yoksa bu gece bitir — yarın ölçüm için tam gün lazım.

---

# Cuma 25 — KALİBRASYON VE ÖLÇÜM 🔴
### 🏁 Gün hedefi: 180 olayın tamamı cebinde

### T-16 · CPU yükü kalibrasyonu
- [ ] `calibrated_work()` — LCG + `volatile` sink
- [ ] İterasyon başına süreyi `timer_us()` ile ölç
- [ ] 2 ms ve 5 ms için iterasyon sayılarını bul
- [ ] **Gerçek ölçülen süreleri** not et (hedefe eşit olmayabilir — sorun değil, raporlanır)
- [ ] Derleme optimizasyonu değişirse yeniden kalibre
- **Kanıt:** Ölçülen süreler tabloda
- **Gereksinim:** FR-23…25

### T-17 · Deneme koşusu
- [ ] S0 ve S3'ü baştan sona koş
- [ ] **Gerçek** telemetri periyodunu ölç (planlanan ≠ gerçekleşen)
- [ ] ✅ **`t₄−t₃` senaryolar arası sabit mi?** — tasarımın kendi kendini sınama testi
- [ ] Sapma varsa **T-08'e dön**, ölçüme başlama
- **Kanıt:** İki CSV + `t₄−t₃` karşılaştırması
- **Gereksinim:** MR-05, tasarım §12

### T-18 · Gerçek ölçüm — 6 senaryo 🔴
- [ ] Sıra: **S0 → S1 → S2 → S3 → S4 → S5**
- [ ] Her senaryo: seç → başlat → 5 sn ısın → **30 basış** → otomatik dur → CSV
- [ ] Basışlar arası ≥ 0.5 sn, **düzensiz aralıklarla** (ritim tutma!)
- [ ] Hiçbir ayar değiştirilmedi (MR-07)
- [ ] Her CSV'yi yazar yazmaz **commit et** — kazayla kaybetme
- **Kanıt:** `S0.csv`…`S5.csv`, toplam ≥ 180 olay
- **Gereksinim:** MR-01…10
- **Süre:** ~25 dakika saf ölçüm + hazırlık. Acele etme.

> **Gün sonu kontrolü — haftanın en önemli anı.** Altı CSV depodaysa ödevin değerlendirilen kısmı güvende. Cumartesi artık sadece anlatım.

---

# Cumartesi 26 — Sunum (tam gün)
### 🏁 Gün hedefi: Her şey depoda, teslim edilebilir

### T-19 · GUI
- [ ] Port seçimi, bağlan/kes
- [ ] Senaryo seçici + START / STOP / DUMP
- [ ] Durum göstergesi (state, senaryo, olay sayısı)
- [ ] "Butona basıldı · Olay N" log paneli
- [ ] Sayaç paneli
- [ ] Ölçüm sürerken komut butonları pasif
- **Kanıt:** Arayüzden kısa bir canlı koşu yönetildi
- **Gereksinim:** UI-01, UI-06, UI-07, UI-10, UI-13

### T-20 · Grafik modülü
- [ ] `plots.py` → `plot_response_times()`, `plot_stage_breakdown()` → `Figure`
- [ ] `gui.py` içine `FigureCanvasTkAgg` ile göm
- [ ] CSV dosyası seçici (tek / çoklu senaryo)
- [ ] PNG kaydetme → `analysis/plots/`
- [ ] `make_plots.py` bağımsız betik
- [ ] `status != ok` dışlama + dışlanan sayının altyazıda gösterimi
- [ ] Eksen birimleri, n, senaryo kimliği
- **Kanıt:** Arayüzdeki grafik ile `make_plots.py` çıktısı **aynı**
- **Gereksinim:** UI-20…28, AR-06…08

### T-21 · Analiz
- [ ] Senaryo başına: n, R min/ort/**max**, 20 ms ihlal sayısı
- [ ] Drop / tx_error / timeout / rec_ovf
- [ ] Aşama sürelerinin senaryolar arası karşılaştırması
- [ ] Kuyruk high-water + gerçek telemetri hızı
- [ ] `summary.csv`
- **Kanıt:** PNG'ler + özet tablo
- **Gereksinim:** AR-01…05, AR-11

### T-22 · Doxygen
- [ ] `Doxyfile` — tasarım §10.2 ayarları
- [ ] `@defgroup`: timing, isr, tasks, record, proto, expfsm
- [ ] Hafta boyunca yazılan yorumların eksiklerini tamamla
- [ ] `doxygen Doxyfile` → **sıfır uyarı**
- [ ] Çıktıyı `docs/doxygen/` altına commit
- **Kanıt:** Uyarısız üretim + açılan HTML
- **Gereksinim:** DOC-01…11

### T-23 · `report.md`
- [ ] Özet tablo
- [ ] **Hipotez (tasarım §12) vs. gözlenen** — uyuştu mu, uyuşmadıysa neden
- [ ] Dört soru: Hangi bileşen değişti? Neden? Hangi ölçüm destekliyor? Ne bilinmiyor?
- [ ] Gözlenen max ≠ kanıtlanmış worst-case
- [ ] CPU süresi vs. duvar saati farkı
- **Gereksinim:** AR-09, AR-10, AR-12, AR-13

### T-24 · Dokümantasyon
- [ ] `README.md` — kart, kablolama, araç sürümleri, derleme/yükleme, arayüz, senaryo seçimi, timer + FreeRTOS ayarları, doxygen komutu, bağlantılar
- [ ] `README.md` — **şartnameden sapma** (çalışma zamanı komut yolu) gerekçesi
- [ ] `setup.md` — sıfırdan kurulum
- [ ] `code-notes.md` — ISR, görevler, TC yolu, zaman hesapları, **kod bloklarıyla**
- [ ] `ai-usage.md` — hangi işlerde destek, nasıl doğrulandı, ne değiştirildi
- **Gereksinim:** DR-03…06, DOC-11

### T-25 · Video
- [ ] Fiziksel basış → arayüzde yanıt
- [ ] Senaryo değişimi, frekans farkı görünsün
- [ ] Grafikler üzerinden ölçüm yorumu
- [ ] Kritik kod bloklarının anlatımı
- **Gereksinim:** DR-08
- **Not:** Videodaki canlı koşu **kısa** olabilir; asıl veri Cuma alındı. 30 basış tekrarlamana gerek yok.

---

# Pazar 27, 08.00–11.00 — Teslim

### T-26 · Son kontrol ve gönderim
- [ ] Tüm dosyalar push edildi
- [ ] Depo eğitmene erişilebilir (özelse erişim verildi)
- [ ] Gereksinimler §9 kabul listesi **6/6**
- [ ] Commit SHA al
- [ ] Telegram'a: repo linki + video
- **Gereksinim:** DR-01, DR-02, DR-07, DR-09

---

## Kritik yol

```
T-01…T-04 ─► T-05…T-07 ─► T-08 🔴 ─► T-10 🎯 ─► T-12…T-14 ─► T-15 ─► T-17 ─► T-18 🔴
  Pzt          Sal          Çar        Çar        Per          Per      Cum      Cum
                                                                                   │
                                          T-19…T-25 ◄───────────────────────────────┘
                                             Cmt
```

**Ertelenemez:** T-03 (donanım), T-08 (TC yolu), T-18 (ölçüm).
**Gerekirse kısılabilir:** T-19 (GUI sadeleşir), T-20 (2 grafikle yetinilir), T-25 (video kısalır).

---

## Kurtarma planları

| Ne zaman | Belirti | Ne yapılır |
|---|---|---|
| **Çarşamba gecesi** | T-10 bitmedi, beş damga görünmüyor | Perşembe'yi kurtarmaya ayır; T-19 GUI'yi baştan feda et, minimal betikle devam |
| **Perşembe gecesi** | Komut protokolü (T-12/13) çalışmıyor | **Derleme zamanı `#define SCENARIO`'ya geri dön.** 6 kez derle-yükle. Demo güzelliğini kaybedersin, veriyi kaybetmezsin. `README`'ye yaz. |
| **Cuma gecesi** | Ölçüm bitmedi veya veri şüpheli | Cumartesi sabahı yedek ölçüm penceresi; sunum öğleden sonraya sıkışır |
| **Cumartesi akşamı** | Her şey yetişmiyor | Öncelik sırası: **ham veri > rapor > code-notes > video > GUI güzelliği > Doxygen HTML** |
| **Herhangi bir an** | Kart bozuldu | S0–S3 asgari veri setidir; S4/S5 eksikse `report.md`'de açıkça belirt — gizleme |

---

## Her gün sonunda 30 saniye

- [ ] Bugünün kanıtları alındı mı?
- [ ] `git commit` + `push` yapıldı mı?
- [ ] Yarının ilk görevi net mi?

**Commit alışkanlığı kritik:** Cuma günü aldığın CSV'ler tek nüsha. Her senaryo sonrası commit et.
