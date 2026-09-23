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

**İlerleme:** 24 / 26 · Kalan: **video** ve **teslim** (ikisi de kullanıcıya ait)

> ⏩ **23.09.2026 Çarşamba:** T-16 kalibrasyon, T-18 resmî ölçüm, T-21 analiz, T-22 Doxygen,
> T-23 rapor ve T-24 dokümantasyon tamamlandı. Plan Cuma'yı ölçüm günü olarak öngörüyordu;
> iki gün önde bitti.
>
> | Görev | Sonuç |
> |---|---|
> | T-16 CPU kalibrasyonu | S4 = 2 001,6 µs · S5 = 5 008,4 µs (hedeften %0,2 sapma) |
> | T-17 deneme koşusu | S0/S3 ön ölçüm + protokol dışı tam koşu (arşivde) |
> | T-18 **resmî ölçüm** | 6 senaryo × 30 olay; basış aralıkları 0/29 ihlal |
> | T-21 analiz | `analysis/ozet-tablo.md`, `summary.csv`, iki grafik |
> | T-22 Doxygen | 0 uyarı, 121 sayfa HTML |
> | T-23 rapor | `analysis/report.md` |
> | T-24 dokümantasyon | README, setup, code-notes, ai-usage, ölçüm protokolü |

> ⏩ **22.09.2026 Salı gecesi ek:** T-15 (minimal PC betiği) yerine doğrudan tam arayüz yazıldı (T-19); grafik modülü (T-20) de kullanıcının isteğiyle öne çekildi.
> - `interface/`: `serial_link.py`, `protocol.py`, `recorder.py`, `plots.py`, `gui.py`, `make_plots.py`, `main.py`
> - Arayüz: senaryo seç → Başlat (SCEN→START sırası; koşarken STOP→bekle→SCEN), telemetri hızı **kart saatinden**, UART hat doluluğu çubuğu, "Butona basıldı · Olay N", DONE'da **otomatik** döküm → CSV → grafik
> - CSV: hedef < 30 → `measurements/deneme/`; var olan dosya `arsiv/`'e taşınır, üzerine yazılmaz; `summary.csv` her resmî koşuda yeniden üretilir
> - Grafikler: olay→R (senaryo başına panel, 20 ms çizgisi), aşama dağılımı (yığılmış sütun), tablo görünümü; fare üstü ipucu; PNG → `analysis/plots/`
> - Çevrimdışı test: `protocol` + `recorder` 15/15 geçti (gerçek e2e kaydıyla)

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

### T-03 · Donanım doğrulama ✅
- [x] FT232 kablolandı: TXD→PA3, RXD→PA2, GND→GND, VCC bağlı değil → **COM7**
- [x] Öz-test firmware'i (`hw_selftest.c`, `-DHW_SELFTEST=ON`) STM32_Programmer_CLI ile SWD'den yüklendi
- [x] UART: açılış satırı bozulmadan okundu; SYSCLK 168 MHz, PCLK1 42 MHz, PCLK2 84 MHz
- [x] Buton: boşta 0, basılı **1 → aktif-HIGH** (HW-04)
- [x] Zıplama ölçüldü: 6 basışta **9 kenar** (bir basışta 3'e kadar) → FR-12 filtresi gerekli
- [x] LED'ler: yeşil (buton), turuncu, kırmızı, mavi — kullanıcı gözle doğruladı
- **Kanıt:** ✅ `hafta-01/docs/kanitlar/t03-t04-hwtest.log`
- **Gereksinim:** HW-01…04

### T-04 · TIM2 zaman kaynağı ✅
- [x] `TIM2->CNT` doğrudan okuma; ⚠️ `HAL_TIM_Base_Start(&htim2)` gerekli (tasarım §2.1)
- [x] 1 s aralık TIM2 ve DWT->CYCCNT ile bağımsız ölçüldü: **fark 1 µs / 1 000 770 µs**
- [x] Sarma: `0x00000010 − 0xFFFFFFF0 = 32` ✓
- **Kanıt:** ✅ aynı log, `HWTEST,TIM` ve `HWTEST,WRAP` satırları
- **Gereksinim:** FR-68, FR-72, FR-73

> **Gün sonu kontrolü:** Terminalde kartın mesajını görüyor musun? Butonun hangi seviyede olduğunu biliyor musun? Timer 1 µs sayıyor mu? Üçü de evet ise yarına hazırsın.

---

# Salı 22 — Ölçüm çekirdeği I
### 🏁 Gün hedefi: 3 görev ayakta, buton olayı göreve ulaşıyor

### T-05 · Veri yapıları ve kayıt defteri ✅
- [x] `ButtonEvent`, `TxMsg` (tampon **96 bayt** — REC satırı 83'e çıkabiliyor), `EventRecord`, `RecStatus`
- [x] `rec_open()`, `rec_stamp()`, `rec_close()`, `rec_read()`, `rec_reset()` — `record.c`
- [x] `event_id` doğrulaması; slot yalnızca `REC_FREE` iken açılır
- [x] ISR/görev sayaç ayrımı: `g_cnt_isr` / `g_cnt_task` — `app_diag.h`
- [x] Doxygen yorumları kodla birlikte yazıldı
- [x] SWD tanı aracı: `hafta-01/tools/read_diag.py` (UART trafiği eklemeden okur)
- **Kanıt:** ✅ Açılış öz-testi: 100 sahte olaydan **64 açıldı, 36 reddedildi**, tüm kurallar geçti
- **Gereksinim:** FR-60…62, DOC-06, tasarım §3, §3.1

### T-06 · EXTI0 ISR + debounce ✅
- [x] t₀ `EXTI0_IRQHandler`'ın ilk satırında, HAL bayrağa dokunmadan (`button_irq_entry`)
- [x] 30 ms tekrar-kenar filtresi; ilk olay muaf
- [x] `event_id` → `rec_open()` → `xQueueSendFromISR`, dönüş kontrolü, drop kaydı
- [x] ⚠️ **Yeni: bırakış zıplaması bulundu** — 5 basışta 8 olay. **FR-12b "yeniden silahlanma"** eklendi
- **Kanıt:** ✅ Aynı 5 basış: önce **8** kabul, düzeltmeden sonra **5** kabul + `unarmed_rej = 1`
- **Gereksinim:** FR-10…18, FR-12b, DOC-04, DOC-05, tasarım §4.1.1

### T-07 · Üç görev + kuyruklar ✅
- [x] `buttonQ` (8 × `ButtonEvent`), `txQ` (16 × `TxMsg`), kuyruk kayıt defterinde isimli
- [x] `TelemetryTask` (3), `ButtonTask` (2), `UartTxTask` (1) — native `xTaskCreate`
- [x] ButtonTask: t₁ + 50 ms bakım turu; Telemetry: S0 gibi süresiz bloklu; UartTx: `txQ`'da bloklu
- [x] Stack taşması / heap tükenmesi kancaları: kırmızı LED + `g_diag.fault`
- [x] ⚠️ **Hata bulundu ve düzeltildi:** defaultTask zombi olarak koşuyordu (görev sayısı 5)
- **Kanıt:** ✅ `task_count = 4`; Idle 912 bayt geri verdi; **t₁ − t₀ = 11–14 µs**
- **Gereksinim:** FR-01…06, tasarım §13.3

> Kanıt dosyası: `hafta-01/docs/kanitlar/t05-t07-diag.md`

> **Gün sonu kontrolü:** Butona bastığında `ButtonTask` uyanıp LED yakıyor mu? Evet ise ISR→kuyruk→görev zinciri çalışıyor demektir.

---

# Çarşamba 23 — Ölçüm çekirdeği II 🎯
### 🏁 Gün hedefi: Tek bir basışın t₀…t₄'ünü görmek

> ⏩ **T-08…T-14 Salı gecesi, plandan iki gün önce tamamlandı.** Kanıt: `hafta-01/docs/kanitlar/t08-t13-e2e-s0-s3.log`

### T-08 · UartTxTask + DMA + **TC yolu doğrulaması** ✅ 🔴
- [x] `s_tx_buf` kalıcı `static` tampon (.bss → SRAM, CCMRAM 0 B)
- [x] `HAL_UART_Transmit_DMA` + `ulTaskNotifyTake(1 s)`; hata/zaman aşımında kayıt kapatılır
- [x] t₄ = USART2 ISR giriş zamanı (`uart_irq_entry`), TC geri çağrısında kayda yazılır
- [x] **HAL 1.8.5 kaynağı okundu:** normal modda `UART_DMATransmitCplt` geri çağrı **yapmaz**, yalnızca `TCIE` açar; `TxCpltCallback` USART TC bayrağında `UART_EndTransmit_IT`'ten gelir
- [x] **Ölçümle doğrulandı:** DMA kesmesi → TC arası **173 µs = 2 karakter** (DR + kaydırma register'ı)
- **Kanıt:** ✅ t₄ − t₃ = **5 551–5 559 µs**, S0 ve S3'te **sabit**. Gerçek baud 115 385 (BRR yuvarlaması, +%0,16) → 640 bit = 5 547 µs + başlatma yükü
- **Gereksinim:** FR-40…46, risk R-2 **kapandı**

### T-09 · 64 bayt mesaj formatlama ✅
- [x] `proto_fmt_fixed()` — boşluk dolgusu + LF; 63 bayt aşılırsa kesmez, `false` döner (`fmt_err_*`)
- [x] Kontrol satırları için `proto_fmt_line()` (değişken uzunluk, ≤ 127 bayt)
- **Kanıt:** ✅ S1/S2/S3'te toplam ~430 TEL satırı, **64 bayt olmayan: 0**
- **Gereksinim:** FR-50…53

### T-10 · Uçtan uca tek olay ✅ 🎯
- [x] `ButtonTask`: t₁ → BTN → t₂ → `txQ` (t₂ kayda gönderimden sonra yazılır; öncelik sırası bunu güvenli kılar)
- [x] **Beş damganın beşi de dolu**, 10 olayın hepsi `ok`
- **Kanıt:** ✅ S0: **R = 5.70–5.71 ms**. S3: **R = 5.69–10.82 ms**, büyüyen aşama **t₃ − t₂** (109 → 5 236 µs)
- ⚠️ **Bulgu:** S0'da boş kuyrukta t₃ − t₂ = 125 µs — ButtonTask'ın gönderim sonrası bakım turu (stack taraması) UartTx'i geciktiriyordu. Bakım olaydan sonraya değil, boşta çalışacak şekilde taşındı.
- **Gereksinim:** FR-30…35

> **Gün sonu kontrolü — haftanın dönüm noktası.** Beş damgayı görebiliyorsan plan sağlam. Göremiyorsan **yarın sabah bana yaz**, Perşembe'yi kurtarma gününe çeviririz.

---

# Perşembe 24 — Deney kontrolü + minimal PC
### 🏁 Gün hedefi: Tam bir senaryo koşusu, elinde ilk CSV

### T-11 · Sayaçlar ✅
- [x] ISR (11) ve görev (10) sayaçları ayrı yapılarda, tek yazar kuralıyla (`app_diag.h`)
- [x] `txq_hwm`: UartTxTask, her alımdan önce (±1). S3'te **2**
- [x] Stack high-water mark, heap en düşük değer: `read_diag.py`
- **Kanıt:** ✅ `CNI` / `CNT` satırları dökümle birlikte geliyor; `txq_drop_*` kasıtlı doldurma testi T-17'de
- **Gereksinim:** FR-64, FR-65

### T-12 · Komut alma (RX) ✅
- [x] `HAL_UART_Receive_IT` bayt bayt; LF'de satır ISR'dan göreve bayrakla devredilir
- [x] Taşma → `rx_overflow`, meşgulken yeni komut → `cmd_busy`, UART hatası → `rx_err` + yeniden başlatma
- [x] HAL 1.8.5'te TX-DMA / RX-IT ortak kilit yok (kaynakta doğrulandı)
- **Kanıt:** ✅ `CMD,STAT` → `STA`; `CMD,BOGUS` → `ERR,unknown`; yanlış durumda `START` → `ERR,state`
- **Gereksinim:** FR-80…84, FR-92

### T-13 · Deney durum makinesi + LED ✅
- [x] `IDLE → ARMED → WARMUP → MEASURING → DRAINING → DONE`, `experiment.c`
- [x] `exp_tick()` ButtonTask bakım turunda; **4. görev yok**
- [x] SCEN yalnızca IDLE/ARMED/DONE'da ve telemetri tamamen durmuşken (`ERR,busy`)
- [x] 5 s ısınma; ölçüm dışındaki basışlar `idle_press` sayılır, kayıt açılmaz
- [x] `CMD,START,n` ile hedef olay sayısı (varsayılan 30, en fazla 64)
- [x] TelemetryTask: S0'da süresiz bloklu, diğerlerinde `vTaskDelayUntil` (FreeRTOS 10.3.1)
- **Kanıt:** ✅ Kartın ölçtüğü periyot S1 **100.00**, S2 **20.00**, S3 **10.00** ms (ort.); S0 ve S3'te 5'er olayla tam koşu, kendiliğinden DONE
- **Gereksinim:** FR-85…90, FR-20…22

### T-14 · Kayıt dökümü ✅
- [x] DRAINING: telemetri durur → `txQ` boşalır → UART boşta → açık kayıt kalmaz (ya da 1 s → `timeout`)
- [x] `CMD,DUMP` → `REC` + `TST` (gerçek telemetri periyodu, iş süresi) + `CNI` + `CNT` + `END`
- [x] Alınamamış damga boş (`have` bitleri)
- **Kanıt:** ✅ `REC,S0,1,476844878,476844890,476844906,476845031,476850584,ok`
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

### T-16 · CPU yükü kalibrasyonu ✅ *(23.09.2026)*
- [x] `calibrated_work()` — LCG + `volatile` sink; `vTaskDelay` yok, kesme kapatma yok
- [x] İterasyon süresi **deney koşullarında** ölçüldü: **125,4 ns** (sıcak flash önbelleği)
- [x] Açılıştaki soğuk-önbellek ölçümü (143,1 ns) %14 yanıltıcı çıktı; kalibrasyon koşan deneyden alındı
- [x] S4 = 15 949 iterasyon, S5 = 39 873 iterasyon
- [x] ⚠️ **Yeni sabit ilk yüklemede flash'a gitmemişti** (ninja, başlık düzenlemesiyle aynı saniyede koştuğu için yeniden derlemedi). Artık `k_scen` tablosu **flash'tan okunarak** doğrulanıyor.
- **Kanıt:** ✅ Ölçülen iş süresi **S4 = 2 001,6 µs (+0,1 %)**, **S5 = 5 008,4 µs (+0,2 %)**; periyot 10 000 µs → ek CPU talebi %20,0 ve %50,1
- **Gereksinim:** FR-23…25

### T-16b · `txq_hwm` ölçüm anlık görüntüsü ✅
- [x] Canlı `txq_hwm` döküm sırasında da artıyordu: S2 resmî koşusunda 16/16 (kuyruk kapasitesi) göründü, oysa 50 Hz'de hat %28 dolu
- [x] DRAINING'e geçerken anlık görüntü alınıp `CNT` satırında o bildiriliyor
- **Gereksinim:** FR-65, AR-11

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
