# Yük altında buton yanıt süresi — ölçüm raporu

**Kart:** STM32F4DISCOVERY (STM32F407VGT6 @168 MHz) · **RTOS:** FreeRTOS 10.3.1
**Ölçüm:** 23 Eylül 2026, tek oturum, tek firmware · **Olay sayısı:** 6 × 30 = 180
**Ham veri:** `measurements/S0…S5.csv` · **Tablolar:** `analysis/ozet-tablo.md` (üreten: `analysis/analyze.py`)
**Grafikler:** `analysis/plots/` (üreten: `interface/make_plots.py`)

---

## 1. Tek cümlelik sonuç

Telemetri yükü arttıkça yanıt süresi **5,62 ms'den 8,25 ms'ye** kadar kademeli olarak büyüdü ve büyümenin tamamı **tek bir aşamadan**, buton mesajının TX kuyruğunda beklemesinden (`t₃−t₂`) geldi. S5'te (100 Hz + %50 CPU yükü) sistem nitel olarak farklı bir rejime geçti: kuyruk doydu, yanıt süresi **115 ms ortalamaya** çıktı, 30 olayın 6'sı hiç gönderilemedi ve 20 ms deadline'ı **23 kez** ihlal edildi.

## 2. Zaman damgaları — neyi ölçüyoruz

Bir buton basışının yanıtı sistemde dört durak geçer. Her durakta **aynı kart
saatinden** (TIM2, 1 MHz) bir zaman damgası alınır. Toplam beş damga vardır:

```
   parmak                                                         son bit
   butona                                                         hattan
   basar                                                          çıkar
     │                                                              │
     ▼                                                              ▼
  ───●──────────●───────────●──────────────●──────────────────────●────►  zaman
     t₀         t₁          t₂             t₃                     t₄
     │          │           │              │                      │
  EXTI0      ButtonTask  yanıt hazır    UART gönderimi        TC kesmesi
  kesmesi    olayı aldı  kuyruğa        başlatılmak üzere     (aktarım bitti)
  girişi                 verilmeden
```

| Damga | Tam olarak nerede alınır | Neyi işaretler |
|---|---|---|
| **t₀** | `EXTI0_IRQHandler`'ın ilk satırı, HAL bayrağı temizlemeden önce | Zıplama filtresinin **kabul ettiği** buton kenarı |
| **t₁** | `ButtonTask`, `xQueueReceive` döner dönmez | Olayın göreve ulaştığı an |
| **t₂** | `xQueueSend` çağrısından hemen önce | Yanıt mesajının hazır olduğu an |
| **t₃** | `HAL_UART_Transmit_DMA` çağrısından hemen önce | Gönderimin başlatılmak üzere olduğu an |
| **t₄** | USART2 **TC** kesmesinin girişi | Son stop bitinin hattan çıktığının gözlendiği an |

Aradaki farklar, gecikmenin **hangi aşamadan** geldiğini söyler:

| Aralık | Adı | İçerdiği süre |
|---|---|---|
| **t₁ − t₀** | görev bekleme | Kesmenin kalanı + bağlam geçişi + `ButtonTask`'ın CPU'yu bekleme süresi. Yüksek öncelikli bir görev CPU'yu tutuyorsa burası büyür. |
| **t₂ − t₁** | yanıt hazırlama | 64 baytlık mesajın biçimlendirilmesi. Görev bu sırada kesilirse (preemption) burası da büyür. |
| **t₃ − t₂** | TX kuyruğu + başlatma | Mesajın `txQ`'da sırasını beklemesi, `UartTxTask`'ın CPU'ya erişmesi ve DMA'yı başlatması. **Hat meşgulse asıl bekleme buradadır.** |
| **t₄ − t₃** | UART hattı + TC | 64 baytın hattan fiziksel olarak akması (ayar 115 200, gerçekte 115 385 baud — §4.1). Yüke değil baud'a bağlıdır. |
| **R = t₄ − t₀** | **yanıt süresi** | Kart tarafında gözlenen toplam süre. Ödevin deadline'ı: ≤ 20 ms. |

Farklar işaretsiz 32-bit aritmetiğiyle hesaplanır; sayaç 71,6 dakikada sardığı
için bu, sarma anına denk gelen olaylarda da doğru sonucu verir.

**Neyin ölçülmediği de önemlidir.** t₀ parmağın butona değdiği an değil, kesmenin
gördüğü elektriksel kenardır. t₄ ise son bitin çıkışı değil, o çıkışı bildiren
kesmenin gözlem anıdır (aradaki fark §8'de). Bu iki sınır, ölçülen R'nin gerçek
uçtan uca gecikmeden biraz **dar** olduğu anlamına gelir.

## 3. Özet tablo

| Senaryo | Telemetri | Ek CPU | n (ok) | R min | R ort | R medyan | R max | >20 ms | Kayıp |
|---|---|---|---|---|---|---|---|---|---|
| **S0** | kapalı | — | 30 | 5,61 | **5,62** | 5,62 | 5,62 | 0 | 0 |
| **S1** | 10 Hz | — | 30 | 5,61 | **5,62** | 5,62 | 5,80 | 0 | 0 |
| **S2** | 50 Hz | — | 30 | 5,61 | **6,99** | 5,62 | 11,19 | 0 | 0 |
| **S3** | 100 Hz | — | 30 | 5,61 | **7,52** | 7,65 | 11,03 | 0 | 0 |
| **S4** | 100 Hz | ≈2 ms | 30 | 5,62 | **8,25** | 8,31 | 12,47 | 0 | 0 |
| **S5** | 100 Hz | ≈5 ms | **24** | 11,42 | **115,03** | 127,76 | **164,71** | **23** | **6** |

Birim: ms. Yalnızca `status = ok` olaylar; kayıplar ayrı sütunda ve **deadline karşılandı sayılmadı** (AR-03). S5'teki 6 olay `tx_drop`: kuyruk dolu olduğu için yanıt mesajı hiç gönderilemedi.

## 4. Aşama analizi — hangi bileşen değişti?

Ortalama süreler (µs, yalnızca `ok`):

| Senaryo | t₁−t₀ (görev bekleme) | t₂−t₁ (hazırlama) | **t₃−t₂ (TX kuyruğu)** | t₄−t₃ (UART hattı) |
|---|---|---|---|---|
| S0 | 12 | 17 | **34** | 5 555 |
| S1 | 12 | 17 | **41** | 5 554 |
| S2 | 12 | 16 | **1 406** | 5 554 |
| S3 | 12 | 17 | **1 938** | 5 553 |
| S4 | **88** | 17 | **2 596** | 5 553 |
| S5 | **886** | **227** | **108 358** | 5 554 |

### 4.1 Sabit kalan: UART hat süresi

`t₄−t₃` altı senaryoda **5 553–5 555 µs**; yükten tamamen bağımsız. Beklenen sonuç budur, çünkü hat süresi baud hızına bağlıdır:

```
64 bayt × 10 bit / 115 385 baud = 5 547 µs      (+ başlatma ve ISR gözlem yükü)
```

Gerçek baud 115 385'tir (nominal 115 200 değil): HAL'in BRR hesabı 42 MHz APB1 saatinde 22,75 bölen üretir, bu da %0,16 hızlı bir hat demektir.

**Bu sabitlik tasarımın kendi kendini sınama testiydi.** `t₄−t₃` yükle birlikte değişseydi, ya zaman damgası yanlış yerden alınıyor ya da DMA beklenmedik biçimde gecikiyor olurdu. Değişmedi.

### 4.2 Telemetri frekansının etkisi: `t₃−t₂`

Buton mesajı TX kuyruğuna girdiğinde, o an hatta olan telemetri mesajının bitmesini bekler. Bekleme 0 ile 5,55 ms arasında, **basışın telemetri periyoduna göre fazına** bağlıdır. Ölçülen çarpışma oranları teorik hat doluluğuyla uyumlu:

| Senaryo | Çarpışmasız olay | Çarpışan | Ölçülen oran | Teorik hat doluluğu |
|---|---|---|---|---|
| S1 | 29 | 1 | %3 | %6 |
| S2 | 18 | 12 | %40 | %28 |
| S3 | 10 | 20 | %67 | %55 |
| S4 | 7 | 23 | %77 | %55 |
| S5 | 0 | 24 | %100 | %55 |

Ölçülen oranın teoriden yüksek çıkması beklenen bir durumdur: teorik değer yalnızca telemetriyi sayar, oysa buton mesajlarının kendisi de hattı kullanır ve S4/S5'te CPU yükü `UartTxTask`'ı geciktirerek hattın meşgul kaldığı pencereyi uzatır.

R dağılımı bu yüzden **iki tepelidir**: çarpışma olmayan olaylar 5,62 ms'de toplanır, çarpışanlar 5,6–11,2 ms arasına yayılır (bkz. `plots/r_per_event.png`, S2 ve S3 panelleri).

### 4.3 CPU yükünün etkisi: `t₁−t₀` ve `t₂−t₁`

S0–S3'te `t₁−t₀` sabit 12 µs'dir: kesmenin kalanı, bağlam geçişi ve görevin uyanması. S4 ve S5'te ilk kez büyür, çünkü `TelemetryTask` (öncelik 3) CPU'yu `ButtonTask`'tan (öncelik 2) önce alır. Basış, telemetrinin hesap yaptığı ana denk gelirse görev hesabın bitmesini bekler:

| | ek CPU işi | t₁−t₀ ortalama | t₁−t₀ **en büyük** |
|---|---|---|---|
| S4 | 2 001 µs | 88 µs | **1 295 µs** |
| S5 | 5 008 µs | 886 µs | **4 926 µs** |

**En büyük değerin iş süresinden küçük olması tesadüf değil, teorinin öngörüsüdür:** en kötü durumda basış hesabın hemen başında gelir ve görev hesabın tamamını bekler. Ölçülen maksimumlar (1 295 < 2 001 ve 4 926 < 5 008) bu sınırın hemen altındadır.

S5'te `t₂−t₁` de büyüdü (17 → 227 µs, en büyük 5 065 µs): `ButtonTask` yanıt mesajını hazırlarken bir sonraki telemetri periyodu başlayıp onu kesiyor.

## 5. S5: aşırı yük rejimi

S5 yalnızca "daha yavaş" değil, **niteliksel olarak farklıdır**. `t₃−t₂` olay sırasına göre düzenli biçimde tırmanır ve ~160 ms'de doyar:

| Olay sırası | 1 | 5 | 10 | 15 | 17 | 24 |
|---|---|---|---|---|---|---|
| t₃−t₂ (ms) | 9,9 | 44 | 90 | 145 | 159 | 159 |

Bu bir kuyruk doyma eğrisidir. Doyma noktası `16 slot × 10 ms = 160 ms`; ölçülen en büyük değer **159,1 ms**.

### Darboğaz hat değil, CPU sırasıdır

| Ölçü | Değer |
|---|---|
| Gönderilen mesaj | 8 275 (8 260 TEL + 30 BTN − 15 düşen) |
| Ölçüm süresi | 81,5 s |
| Hatta geçen süre | 45,9 s |
| **UART hat doluluğu** | **%56,3** |
| **Servis hızı** | **101,6 mesaj/s** |
| Telemetri üretimi | 100,0 mesaj/s |

Kuyruk tamamen dolu (`txq_hwm = 16/16`) iken hat **yarı boştur**. Sistem bant genişliğine değil, `UartTxTask`'ın CPU'ya erişimine takılmıştır.

### Mekanizma: servis hızının telemetri periyoduna kilitlenmesi

`UartTxTask` en düşük önceliktedir (1) ve ancak `TelemetryTask` bloklandığında koşabilir. 10 ms'lik periyodun 5 ms'sini telemetri kullanır:

```
t = 0,0 ms   TelemetryTask uyanır, 5 ms hesap yapar
t = 5,0 ms   telemetri bloklanır  →  UartTxTask koşar, DMA'yı başlatır
t = 10,5 ms  TC gelir (5,55 ms hat süresi) — ama telemetri t = 10 ms'de yeniden koşuyor
t = 15,0 ms  telemetri bloklanır  →  UartTxTask bir SONRAKİ mesajı başlatabilir
```

Sonuç: **periyot başına tam bir mesaj**, yani 100 mesaj/s. Üretim de 100 mesaj/s. Sistem tam kapasitede, kararsız dengededir; buton mesajları (30 adet) dengeyi bozar, kuyruk geri dönüşsüz dolar ve taşmaya başlar.

Bu yorum iki bağımsız koşuyla desteklenir: aynı gün yapılan ve basış aralıkları çok daha kısa olan ön denemede (`measurements/arsiv/protokol-disi-20260923/`) kuyruk **daha hızlı** doldu (R ort 110,8 ms, 7 kayıp). Basış hızı doymanın *hızını* değiştirir, doymanın *kendisini* değiştirmez.

## 6. Hipotez ile gözlemin karşılaştırması

Tasarım dosyasının §12'sinde, ölçümden **önce** şu hipotez yazılmıştı:

| Hipotez | Gözlem | Sonuç |
|---|---|---|
| `t₄−t₃` yükten bağımsız sabit kalır | 5 553–5 555 µs, altı senaryoda | ✅ doğrulandı |
| Telemetri frekansı `t₃−t₂`'yi büyütür | 34 → 41 → 1 406 → 1 938 µs | ✅ doğrulandı |
| Ek CPU yükü `t₁−t₀` ve `t₂−t₁`'i büyütür | 12 → 88 → 886 µs | ✅ doğrulandı |
| S5'te 20 ms ihlali "muhtemel" | 23 ihlal + 6 kayıp | ⚠️ **hafife alınmıştı** |

Hipotezde eksik kalan nokta: S5 için yalnızca "ihlal muhtemel" denmişti. Gerçekte sistem kararlı bir gecikme artışı değil, **kuyruk taşması ve veri kaybı** üretti. Hipotez, `UartTxTask`'ın düşük önceliğinin servis hızını üretim hızına kilitleyeceğini öngörmemişti.

## 7. Ödevin dört sorusu

**Hangi bileşen değişti?** S1–S4'te değişen tek aşama TX kuyruğunda bekleme (`t₃−t₂`). S4'ten itibaren buna görev bekleme (`t₁−t₀`) eklendi. S5'te değişen şey bir aşamanın süresi değil, **sistemin rejimi**: kuyruk kararlı çalışmaktan çıkıp doydu.

**Neden?** Üç mekanizma: (1) telemetri mesajı hattı 5,55 ms meşgul eder, buton mesajı sırasını bekler; (2) yüksek öncelikli telemetri görevi CPU'yu alarak `ButtonTask`'ı geciktirir; (3) en düşük öncelikli `UartTxTask` yalnızca telemetri bloklandığında koşabildiği için servis hızı telemetri periyoduna kilitlenir.

**Hangi ölçüm destekliyor?** Sırasıyla: aşama tablosu ve çarpışma oranları (§4.2); `t₁−t₀` maksimumunun iş süresiyle sınırlı olması (§4.3); kuyruk doluyken hat doluluğunun yalnızca %56 olması ve servis hızının 101,6 mesaj/s çıkması (§5).

**Ne henüz bilinmiyor?** §8'de.

## 8. Ölçümün sınırları — neyi iddia etmiyoruz

- **Gözlenen maksimum, kanıtlanmış worst-case değildir (AR-10).** n = 30 ve basışlar rastgele fazdadır. S3'te teorik en kötü bekleme 5,55 ms iken en yüksek gözlenen çarpışma 5,45 ms'dir; en kötü duruma yaklaşıldı ama ona ulaşıldığı garanti edilemez.
- **t₀ fiziksel basma anı değildir.** Kesme girişinde, zıplama filtresi tarafından kabul edilen kenarın zamanıdır. Butonun elektriksel kenarı ile parmağın teması arasındaki süre ölçülmedi (logic analyzer gerekirdi).
- **t₄ son bitin çıkışı değil, TC kesmesinin gözlem anıdır.** Aradaki fark ölçüldü: DMA tamamlanması ile TC arası **173 µs**, tam iki karakter süresi (veri register'ı + kaydırma register'ı). t₄ bunun sonrasındadır.
- **İş süresi duvar saatidir, saf CPU süresi değildir (AR-13).** `calibrated_work` en yüksek öncelikli görevde koşar, yani başka görev kesemez; ancak kesmeler (UART TC, DMA, tick) bu süreye dahildir. Saf CPU süresi ölçülenden birkaç mikrosaniye kısadır.
- **Derleme ayarı: Debug, `-O0`.** Görev kodu optimize edilmemiştir; mutlak süreler `-O2` ile daha kısa olurdu. Karşılaştırma geçerlidir çünkü altı senaryo da aynı ikili dosyayla ölçülmüştür, ama mutlak değerler bu ayara özgüdür.
- **Kuyruk yüksek su seviyesi ±1 hassasiyetindedir**; `UartTxTask` derinliği alımdan hemen önce okur.
- **PC saati hiçbir hesaba girmedi.** Tüm damgalar kartın TIM2 sayacından alındı. (Arayüz geliştirilirken PC tarafında 100 Hz telemetrinin 128 Hz olarak sayıldığı görüldü; USB satırları paketler halinde teslim ettiği için PC varış zamanları ölçüme uygun değildir.)

## 9. Kayıplar ve sayaçlar (MR-09 — hiçbiri gizlenmedi)

| Senaryo | tx_drop | btnq_drop | tx_error | timeout | txq_hwm | TEL düşen | debounce_rej | unarmed_rej |
|---|---|---|---|---|---|---|---|---|
| S0 | 0 | 0 | 0 | 0 | 1 | 0 | 1 | 72 |
| S1 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 63 |
| S2 | 0 | 0 | 0 | 0 | 2 | 0 | 1 | 57 |
| S3 | 0 | 0 | 0 | 0 | 2 | 0 | 2 | 62 |
| S4 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 61 |
| **S5** | **6** | 0 | 0 | 0 | **16** | **9** | 0 | 34 |

- `unarmed_rej`: butonun **bırakılırken** zıpladığı kenarlar. Basış sayılsalardı 30 yerine ~90 olay kaydedilirdi; FR-12b filtresi engelledi.
- `debounce_rej`: basış anındaki zıplama (30 ms penceresi).
- `idle_press` altı senaryoda da 0: ısınma sırasında yanlışlıkla basılmadı.
- Telemetri periyodu her senaryoda ortalama **9 999 / 19 999 / 99 999 µs**; planlanan periyottan sapma %0,01'in altında.

## 10. Ne yapılabilirdi

Ölçümün gösterdiği darboğaz mimariden kaynaklanıyor; ödev bu mimariyi şart koştuğu için değiştirilmedi. Yine de S5'teki çöküş şu üç değişiklikten biriyle engellenebilirdi:

1. **Bir sonraki aktarımı TC kesmesinde başlatmak.** Kuyruk boş değilse ISR doğrudan yeni DMA'yı başlatır; servis hızı CPU sırasına bağlı olmaktan çıkar.
2. **`UartTxTask`'a telemetriden yüksek öncelik vermek.** Görev CPU'yu çok kısa süre kullanır (memcpy + DMA başlatma), bu yüzden yüksek öncelik telemetriyi ölçülebilir biçimde geciktirmezdi.
3. **Ek CPU işini daha düşük öncelikli bir göreve taşımak**, yani telemetri üretimini yükten ayırmak.

Bunların hiçbiri denenmedi; rapor ödevde tanımlanan mimariye aittir.
