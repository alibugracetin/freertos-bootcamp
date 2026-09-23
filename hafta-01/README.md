# Hafta 1 — Yük altında buton yanıt süresi

FreeRTOS Bootcamp · Ödev 01 · **Ali Buğra Çetin**

Butona basıldığında kartın UART üzerinden ürettiği yanıtın ne kadar sürede tamamlandığını beş zaman damgasıyla ölçen bir STM32F407 + FreeRTOS uygulaması ve PC arayüzü. Sistem yükü (telemetri frekansı ve CPU işi) altı senaryoda kontrollü biçimde değiştirilerek gecikmenin **hangi aşamada** büyüdüğü ham veriyle gösterilmiştir.

**Sonuç özeti:** Yanıt süresi S0'da 5,62 ms, S4'te 8,25 ms; artışın tamamı TX kuyruğunda beklemeden geliyor. S5'te (100 Hz + %50 CPU) sistem aşırı yük rejimine girdi: R ortalaması 115 ms, 23 deadline ihlali, 6 olay kayıp. Ayrıntı: [`analysis/report.md`](analysis/report.md).

---

## 1. Donanım

| | |
|---|---|
| Kart | STM32F4DISCOVERY (STM32F407VGT6, Cortex-M4F @168 MHz) |
| Buton | Kart üzerindeki **B1 USER** (mavi), **PA0**, aktif-HIGH, donanım debounce **yok** |
| LED'ler | PD12 yeşil · PD13 turuncu · PD14 kırmızı · PD15 mavi (deney durumu) |
| Seri bağlantı | **FT232RL USB-TTL** dönüştürücü, 115200 8N1 |
| Programlama | Kart üzerindeki ST-LINK/V2-1 (SWD) |

**Kablolama.** Kartın ST-LINK'i bir sanal COM port sunar, ancak bu hat F407'nin USART pinlerine bağlı değildir; ölçüm hattı için harici dönüştürücü kullanılmıştır.

| FT232RL | Discovery | Not |
|---|---|---|
| TXD | **PA3** (USART2_RX) | çapraz |
| RXD | **PA2** (USART2_TX) | çapraz |
| GND | GND | zorunlu, ortak referans |
| VCC | *bağlanmaz* | kart USB'den beslenir |

⚠️ FT232RL'in VCCIO seçimi **3,3 V** olmalıdır.

## 2. Yazılım mimarisi

```
[B1/PA0] --EXTI0 ISR--> buttonQ(8) --> ButtonTask(öncelik 2) --+
                          t₀                    t₁, t₂         |
                                                               v
TelemetryTask(öncelik 3) -------------------------------> txQ(16, FIFO)
  periyodik yük üreteci                                        |
                                                               v
                                          UartTxTask(öncelik 1) --> USART2 + DMA
                                                 t₃                   t₄ (TC ISR)
```

- **Üç uygulama görevi** (Idle hariç), preemptive, 3 > 2 > 1.
- **UART'ın tek sahibi** `UartTxTask`'tır; diğer görevler yalnızca kuyruğa mesaj koyar.
- Deney durum makinesi ayrı bir görev değildir; `ButtonTask`'ın bakım turunda çalışır.

### Zaman damgaları

| Damga | Nerede alınır |
|---|---|
| t₀ | EXTI0 kesmesinin ilk satırında, HAL bayrağı temizlemeden önce |
| t₁ | `ButtonTask` olayı kuyruktan alır almaz |
| t₂ | Yanıt `xQueueSend` çağrısından hemen önce |
| t₃ | `HAL_UART_Transmit_DMA` çağrısından hemen önce |
| t₄ | USART2 **TC** kesmesinin girişinde (son bit hattan çıktıktan sonra) |

`R = t₄ − t₀`. Tüm damgalar **TIM2**'den alınır: 32-bit, 1 MHz, ≈71,6 dakikada sarar. Farklar işaretsiz aritmetikle hesaplanır, sarma doğal olarak doğrudur.

## 3. Ayarlar

| Ayar | Değer |
|---|---|
| SYSCLK / APB1 / APB1 timer | 168 MHz / 42 MHz / 84 MHz |
| TIM2 | PSC = 83, ARR = 0xFFFFFFFF → 1 µs |
| USART2 | 115200 8N1 (gerçek baud 115 385), TX: DMA1 Stream6 Ch4, RX: kesme |
| HAL timebase | TIM6 (SysTick FreeRTOS'a ait) |
| NVIC öncelikleri | EXTI0 = USART2 = DMA1_Stream6 = **5** (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`) |
| FreeRTOS | 10.3.1, tick 1000 Hz, heap_4 20 KB, `configUSE_TIMERS = 0` |
| Kuyruklar | `buttonQ` 8 olay · `txQ` 16 mesaj, FIFO |
| Mesaj boyu | TEL/BTN tam 64 bayt (boşluk dolgusu + LF) |
| Debounce | 30 ms tekrar-kenar filtresi **+** 30 ms "bırakılmış görülme" koşulu |
| Derleme | CMake **Debug**, `-O0 -g3`, `--specs=nano.specs` |
| Ek CPU işi | S4: 15 949 iterasyon (ölçülen 2 001,6 µs) · S5: 39 873 iterasyon (5 008,4 µs) |

## 4. Derleme ve yükleme

Araçlar STM32CubeIDE 2.2.0 kurulumundan gelir (CMake, Ninja, arm-none-eabi-gcc, STM32_Programmer_CLI); ayrıca kurulum gerekmez. Yolları PATH'e ekleyin:

```bash
PL="C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins"
export PATH="$PL/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.200.202605190741/tools/bin:$PATH"
```

Ölçüm firmware'i:

```bash
cd hafta-01/firmware
cmake --preset Debug -DHW_SELFTEST=OFF
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/firmware.elf -v -rst
```

Donanım öz-testi (UART, LED, buton, TIM2 doğrulaması; RTOS başlatmaz):

```bash
cmake --preset Debug -DHW_SELFTEST=ON && cmake --build --preset Debug
```

> ⚠️ `HW_SELFTEST` CMake önbelleğinde kalır. Ölçüm firmware'i için **açıkça OFF** verin.
> Başlık dosyası değişikliklerinden sonra sabitlerin karta gerçekten gittiğini doğrulamak için `k_scen` tablosu flash'tan okunabilir (bkz. `docs/code-notes.md`).

## 5. Arayüzü çalıştırma

```bash
pip install -r hafta-01/interface/requirements.txt
python hafta-01/interface/main.py
```

Portu seçip **Bağlan** deyin. Senaryo seçin, hedef olay sayısını girin (resmî ölçüm için 30), **▶ Başlat**. Arayüz `CMD,SCEN` ve `CMD,START` gönderir; deney bitince kayıtları **kendiliğinden** alır ve CSV'leri yazar.

Grafikleri arayüzsüz üretmek için:

```bash
python hafta-01/interface/make_plots.py          # measurements/ → analysis/plots/
python hafta-01/analysis/analyze.py              # → analysis/ozet-tablo.md
```

## 6. Senaryolar ve ölçüm prosedürü

| ID | Telemetri | Ek CPU işi | Hat doluluğu | CPU talebi |
|---|---|---|---|---|
| S0 | kapalı | — | %0 | — |
| S1 | 10 Hz | — | %5,5 | — |
| S2 | 50 Hz | — | %27,7 | — |
| S3 | 100 Hz | — | %55,5 | — |
| S4 | 100 Hz | ≈2 ms | %55,5 | %20,0 |
| S5 | 100 Hz | ≈5 ms | %55,5 | %50,1 |

Koşu sırası ve kuralları: [`docs/olcum-protokolu.md`](docs/olcum-protokolu.md). Özet: senaryo seç → 5 s ısınma → 30 basış (aralarda **en az 0,5 s**, değişken) → otomatik döküm.

## 7. Protokol (kart ↔ PC)

**Kart → PC.** `TEL` ve `BTN` ölçüm mesajlarıdır ve tam 64 bayttır. Diğerleri değişken uzunluktadır ve yalnızca ölçüm dışında gönderilir.

```
TEL,<seq>,<scen>,<t_us>                              (64 bayt)
BTN,<event_id>,<scen>,PRESSED                        (64 bayt)
STA,<durum>,<scen>,<olay>,<hedef>,<periyot_ms>
REC,<scen>,<id>,<t0>,<t1>,<t2>,<t3>,<t4>,<durum>     (alınamamış damga boş)
TST,<scen>,<tel>,<per_min>,<per_ort>,<per_max>,<is_ort>,<is_max>
CNI,... (ISR sayaçları)   CNT,... (görev sayaçları)   END,<scen>,<n>
ACK,...   ERR,<neden>,<komut>   BOOT,...
```

**PC → kart:** `CMD,SCEN,S3` · `CMD,START[,n]` · `CMD,STOP` · `CMD,DUMP` · `CMD,STAT`

## 8. Depo yapısı

```
hafta-01/
├── firmware/          STM32 projesi (CMake); kendi kodumuz Core/Src, Core/Inc
├── interface/         PC arayüzü ve grafik modülü (Python)
├── measurements/      S0…S5.csv, Sx_diag.csv, summary.csv, arsiv/, deneme/
├── analysis/          report.md, ozet-tablo.md, analyze.py, plots/
├── tools/             read_diag.py (SWD ile tanı okuma)
└── docs/              setup.md, code-notes.md, ai-usage.md, olcum-protokolu.md,
                       Doxyfile, doxygen/ (üretilmiş HTML), kanitlar/
```

Süreç belgeleri (gereksinimler, tasarım, görev listesi) deponun kökündeki `specs/` altındadır.

## 9. Şartnameden sapmalar

| Sapma | Gerekçe |
|---|---|
| Senaryo seçimi derleme zamanı `#define` yerine **çalışma zamanı UART komutu** | Şartname ek komut görevini zorunlu tutmuyor, yasaklamıyor. Görev sayısı 3'te tutuldu: komutlar RX kesmesinde toplanır, `ButtonTask` bakım turunda ayrıştırılır. Senaryolar arası geçiş hızlanır ve altı senaryo tek binary ile ölçülür. |
| `xTaskDelayUntil` yerine **`vTaskDelayUntil`** | CubeMX'in getirdiği FreeRTOS 10.3.1'de `xTaskDelayUntil` henüz yok; 10.4'te yeniden adlandırılmış aynı işlev. Mutlak tick tabanı davranışı aynıdır. |
| Debounce filtresine **ikinci koşul** eklendi (FR-12b) | Şartnamedeki 30 ms tekrar-kenar filtresi yalnızca basış anını korur. Ölçümle görüldü ki buton **bırakılırken de** zıplıyor ve bu kenarlar yeni basış sayılıyordu (5 basışta 8 olay). Ek koşul: yeni basış kabul edilmeden önce buton en az 30 ms bırakılmış görülmeli. |
| Proje formatı **CMake** (CubeIDE managed build değil) | CubeMX 6.18.1 betik modunda CubeIDE proje dosyalarını üretemedi. CMake projesi CubeIDE 2.2'nin kendi araçlarıyla derlenir; kaynak, HAL ve `.ioc` aynıdır. |
| `configMAX_PRIORITIES = 56` | CMSIS-RTOS v2 seçiliyken CubeMX bu değeri kilitliyor. Şartnamenin "en az 4" koşulu sağlanıyor; görevler native `xTaskCreate` ile 1/2/3 önceliğinde açılıyor. |

## 10. Belgeler

| Dosya | İçerik |
|---|---|
| [`analysis/report.md`](analysis/report.md) | Ölçüm raporu: sonuçlar, aşama analizi, S5 aşırı yük incelemesi, sınırlar |
| [`docs/code-notes.md`](docs/code-notes.md) | Kritik kod blokları ve neden öyle yazıldıkları |
| [`docs/setup.md`](docs/setup.md) | Sıfırdan kurulum |
| [`docs/olcum-protokolu.md`](docs/olcum-protokolu.md) | Ölçüm koşu prosedürü |
| [`docs/ai-usage.md`](docs/ai-usage.md) | Yapay zekâ kullanımı |
| `docs/doxygen/html/index.html` | Üretilmiş kod dokümantasyonu (sıfır uyarı) |
| `docs/kanitlar/` | Ara doğrulamaların ham kayıtları |

Doxygen'i yeniden üretmek için:

```bash
cd hafta-01/docs && doxygen Doxyfile     # çıktı: docs/doxygen/html/
```
