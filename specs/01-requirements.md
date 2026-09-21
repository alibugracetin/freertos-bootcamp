# 01 — Gereksinimler (Requirements)

**Proje:** FreeRTOS Bootcamp Hafta 1 — Yük Altında Buton Yanıt Süresi Analizi
**Kaynak şartname:** `odev-01.html` (Erhan Konak, 17 Eylül 2026)
**Son teslim:** 27 Eylül 2026 Pazar 11.00 (TRT)
**Durum:** 🟡 TASLAK — kullanıcı onayı bekliyor

> Bu dosya **ne** yapılacağını tanımlar, **nasıl** yapılacağını değil. Tasarım kararları `02-design.md` içindedir.
> Her gereksinim doğrulanabilir olmalıdır: "hızlı olmalı" değil, "≤ 20 ms olmalı".

---

## 1. Amaç

Bir butona basıldığında, mikrodenetleyicinin UART üzerinden ürettiği yanıtın **ne kadar sürede** tamamlandığını ölçmek; sistem yükünü (telemetri frekansı ve CPU işi) kontrollü biçimde değiştirerek bu sürenin **hangi aşamasının** büyüdüğünü ham ölçüm verisiyle kanıtlamak.

Proje çıktısı iki parçalıdır:
1. **Çalışan sistem** — firmware + PC arayüzü.
2. **Mühendislik incelemesi** — ham veri, grafikler ve gerekçeli analiz.

**Kapsam dışı:** Farklı kartlar arasında mutlak hız yarışı. Değerlendirme tekrarlanabilirlik ve açıklamaya dayanır.

---

## 2. Tanımlar

| Terim | Tanım |
|---|---|
| **t₀** | Buton ISR girişinde, debounce filtresi tarafından **kabul edilen** kenarın zamanı. Fiziksel basma anı değildir. |
| **t₁** | `ButtonTask` olayı kuyruktan aldıktan hemen sonraki zaman. |
| **t₂** | Yanıt mesajı için `xQueueSend` çağrısından **hemen önceki** zaman. |
| **t₃** | UART gönderim başlatma çağrısından **hemen önceki** zaman. |
| **t₄** | UART Transmission-Complete (TC) olayı işlenirken alınan zaman. Son bitin çıkışıyla birebir aynı an değildir. |
| **R** | Yanıt süresi. `R = t₄ − t₀`. Kart tarafında gözlenen toplam süre. |
| **Deadline** | R için 20 ms eşiği. Ödeve özgü bir seçimdir, ürün standardı değildir. |
| **Kabul edilen basış** | Debounce filtresinden geçip `buttonQ`'ya başarıyla yazılmış buton olayı. |
| **Senaryo** | S0–S5 arasında, telemetri frekansı ve ek CPU yüküyle tanımlanmış deney koşulu. |
| **Olay kimliği (event_id)** | Her kabul edilen basışa verilen, monoton artan benzersiz numara. Tüm zaman damgaları bu kimlik üzerinden eşleşir. |

---

## 3. Platform varsayımları

| Öğe | Değer | Not |
|---|---|---|
| Kart | **STM32F4DISCOVERY** (STM32F407G-DISC1) | 4 kullanıcı LED'i: PD12 yeşil, PD13 turuncu, PD14 kırmızı, PD15 mavi |
| MCU | STM32F407VGT6 (Cortex-M4F, 168 MHz, FPU) | 1 MB flash, 128 KB SRAM + 64 KB CCM |
| RTOS | FreeRTOS (CMSIS-RTOS v2 veya native API) | CubeMX üzerinden |
| IDE | STM32CubeIDE | |
| Buton | **B1 USER (mavi), PA0** | Aktif-HIGH, harici pull-down. Donanım debounce **yok** → yazılım filtresi zorunlu. |
| Seri bağlantı | **FT232RL USB-TTL dönüştürücü → COM7** | VCCIO 3.3 V. Kartın ST-LINK/V2-1'i PC'de ayrıca bir sanal COM port (COM6) sunar; bu projede kullanılmaz, ölçüm hattı FT232RL'dir. |
| PC arayüzü | Python | `pyserial` + GUI + `matplotlib` |
| Zaman kaynağı | 32-bit donanım timer, 1 MHz | ≈71.6 dakikada sarar |

**Bağlantı (FT232RL ↔ Discovery):**

| FT232RL | Discovery | Not |
|---|---|---|
| TXD | USART RX pini | Çapraz bağlanır |
| RXD | USART TX pini | Çapraz bağlanır |
| GND | GND | **Zorunlu** — ortak referans yoksa veri bozulur |
| VCC | *bağlanmaz* | Kart USB'den beslenir; iki besleme kaynağı birleştirilmez |

| ID | Gereksinim |
|---|---|
| **HW-01** | FT232RL VCCIO seviyesi 3.3 V olmalıdır. |
| **HW-02** | FT232RL VCC hattı karta bağlanmamalıdır; yalnızca TXD / RXD / GND kullanılır. |
| **HW-03** | Seçilen USART pinlerinin kart üzerinde başka bir çevre birimiyle çakışmadığı şemadan doğrulanmalıdır. |
| **HW-04** | Butonun aktif seviyesi, EXTI yapılandırması kesinleşmeden önce ölçümle doğrulanmalıdır. |
| **HW-05** | DMA kullanılırsa, DMA'nın eriştiği tüm tamponlar normal SRAM'de bulunmalıdır; CCM RAM (`0x10000000`) DMA tarafından erişilemez. |

---

## 4. Firmware gereksinimleri (FR)

### 4.1 Görev yapısı

| ID | Gereksinim |
|---|---|
| **FR-01** | Sistem, Idle görevi hariç **tam olarak 3 uygulama görevi** çalıştırmalıdır: `TelemetryTask`, `ButtonTask`, `UartTxTask`. |
| **FR-02** | Görev öncelikleri `TelemetryTask` (3) > `ButtonTask` (2) > `UartTxTask` (1) olmalıdır. Büyük sayı yüksek önceliktir. |
| **FR-03** | Scheduler **preemptive** modda çalışmalıdır (`configUSE_PREEMPTION = 1`). |
| **FR-04** | `configMAX_PRIORITIES` **en az 4** olmalıdır. |
| **FR-05** | UART çevre birimine **yalnızca `UartTxTask`** erişmelidir. `TelemetryTask` ve `ButtonTask` UART API'si çağırmamalıdır. |
| **FR-06** | Hiçbir görev meşgul bekleme (busy-wait) ile CPU tüketmemelidir; bekleme daima bloklayıcı RTOS çağrısıyla yapılmalıdır. |

### 4.2 Buton kesmesi ve debounce

| ID | Gereksinim |
|---|---|
| **FR-10** | Buton kesmesi yalnızca **basış kenarını** (press edge) işlemelidir; bırakma kenarı ölçüme dahil edilmemelidir. |
| **FR-11** | ISR, kesme bayrağını temizledikten sonra monoton timer'ı okuyup **t₀**'ı kaydetmelidir. |
| **FR-12** | Sistem, kabul edilen bir kenardan sonraki **30 ms** içindeki tekrar kenarları reddetmeli ve ayrı bir sayaçta saymalıdır. |
| **FR-12b** | Bir basış kabul edildikten sonra, buton **en az 30 ms kesintisiz bırakılmış** gözlenmeden yeni basış kabul edilmemelidir ("yeniden silahlanma"). Bu koşulla reddedilen kenarlar ayrı bir sayaçta sayılmalıdır. *Gerekçe: T-07'de 5 fiziksel basışta 8 olay kabul edildi; fazlalık üçü de bırakış zıplamasıydı (bkz. tasarım §4.1). FR-12'nin 30 ms penceresi yalnızca basış anındaki zıplamayı kapsar.* |
| **FR-13** | Sistemin **ilk** buton olayı debounce filtresi tarafından yanlışlıkla reddedilmemelidir (ayrı ilk-olay durumu gerekir). |
| **FR-14** | ISR her kabul edilen olaya benzersiz, monoton artan bir `event_id` atamalıdır. |
| **FR-15** | ISR olayı `xQueueSendFromISR` ile `buttonQ`'ya yazmalı, dönüş değerini kontrol etmeli ve başarısızlıkta drop sayacını artırmalıdır. |
| **FR-16** | ISR içinde bloklayıcı çağrı, UART yazımı veya `printf` bulunmamalıdır. |
| **FR-17** | Buton kesmesinin NVIC önceliği, FreeRTOS `FromISR` API'lerini çağırmaya uygun aralıkta olmalıdır (`configMAX_SYSCALL_INTERRUPT_PRIORITY` kuralı). |
| **FR-18** | Kuyruğa giremeyen (drop olan) olayların `event_id` ve `t₀` değerleri ISR-güvenli bir hata kaydında korunmalıdır. |

### 4.3 Telemetri görevi

| ID | Gereksinim |
|---|---|
| **FR-20** | `TelemetryTask` periyodunu mutlak tick tabanlı bekleme (`xTaskDelayUntil`) ile korumalıdır. |
| **FR-21** | Telemetri periyodu senaryoya göre 100 ms (10 Hz), 20 ms (50 Hz) veya 10 ms (100 Hz) olmalıdır. |
| **FR-22** | S0 senaryosunda `TelemetryTask` **bloklanmalıdır**; boş döngüde dönmemelidir. |
| **FR-23** | S4 ve S5 senaryolarında görev, sonucu gerçekten kullanılan ve derleyici optimizasyonuyla silinemeyen, sabit iterasyonlu bir hesaplama işi yapmalıdır (hedef ≈2 ms ve ≈5 ms). |
| **FR-24** | Ek CPU yükü `vTaskDelay` ile üretilmemeli; kesmeler kapatılmamalıdır. |
| **FR-25** | Hesaplama işinin gerçek süresi kart üzerinde ölçülerek kalibre edilmeli ve raporlanmalıdır. |
| **FR-26** | Telemetri mesajı `txQ`'ya bloklamadan (timeout 0) yazılmalı; başarısızlıkta telemetri drop sayacı artırılmalıdır. |

### 4.4 Buton görevi

| ID | Gereksinim |
|---|---|
| **FR-30** | `ButtonTask`, `buttonQ` üzerinde bloklanarak beklemelidir. Deney durum makinesini yürütmek için sınırlı bir timeout kullanılabilir (bkz. `02-design.md` §7); meşgul bekleme yapılmamalıdır. |
| **FR-31** | Olayı alır almaz **t₁** kaydedilmelidir. |
| **FR-32** | Yanıt mesajı hazırlandıktan sonra, `xQueueSend` çağrısından hemen önce **t₂** kaydedilmelidir. |
| **FR-33** | Yanıt mesajı `event_id` değerini taşımalıdır. |
| **FR-34** | `txQ`'ya yazma başarısızsa olay `tx_drop` olarak işaretlenmeli, kayıt kapatılmalıdır. |
| **FR-35** | Mesaj kuyruğa **değerle kopyalanmalı** veya ömrü aktarım sonuna kadar garanti edilmiş bir tampon sahipliği kullanılmalıdır; yerel değişken adresine güvenilmemelidir. |

### 4.5 UART gönderim görevi

| ID | Gereksinim |
|---|---|
| **FR-40** | `UartTxTask` `txQ`'yu FIFO sırayla tüketmelidir. |
| **FR-41** | Gönderim **kesme (IT) veya DMA** ile yapılmalı; görev aktarım tamamlanana kadar bloklanmalıdır. Polling kullanılmamalıdır. |
| **FR-42** | Gönderim başlatma çağrısından hemen önce **t₃** kaydedilmelidir. |
| **FR-43** | UART **Transmission-Complete** olayında **t₄** kaydedilmeli ve görev uyandırılmalıdır. |
| **FR-44** | Başlatma başarısız olursa olay hata olarak işaretlenmeli; görev sonsuz beklememelidir. |
| **FR-45** | Aktarım tamponu, aktarım tamamlanana kadar geçerli kalmalıdır. |
| **FR-46** | Tamamlanma kaydı **doğru `event_id`** ile eşleştirilmelidir. |

### 4.6 Mesaj formatı

| ID | Gereksinim |
|---|---|
| **FR-50** | Her TEL ve BTN mesajı **tam olarak 64 bayt** olmalıdır. |
| **FR-51** | Mesaj, ASCII içerik + boşluk dolgusu ile 63 bayta tamamlanmalı, 64. bayt LF (`\n`) olmalıdır. |
| **FR-52** | İçerik 63 baytı aşarsa sessizce kesilmemeli; hata olarak işaretlenmelidir. |
| **FR-53** | UART yapılandırması **115200 baud, 8N1** olmalıdır. |

### 4.7 Ölçüm kaydı ve dışa aktarım

| ID | Gereksinim |
|---|---|
| **FR-60** | RAM'de **en az 64 olay** kapasiteli bir kayıt havuzu bulunmalıdır. |
| **FR-61** | Havuz dolduğunda taşma sayacı artırılmalıdır; eski kayıtlar sessizce ezilmemelidir. |
| **FR-62** | Kayıt havuzuna ISR ve görev tarafından yapılan erişim güvenli olmalıdır (sahiplik veya kritik bölüm ile). |
| **FR-63** | Her olay sonunda `ok`, `drop`, `tx_error` veya `timeout` durumlarından biriyle kapatılmalıdır. |
| **FR-64** | Sistem şu sayaçları **ayrı ayrı** tutmalıdır: kabul edilen basış, debounce ile reddedilen kenar, buton kuyruğu drop, TX kuyruğu drop (TEL/BTN ayrı), UART hatası, kayıt havuzu taşması. |
| **FR-65** | Kuyrukların yüksek su seviyesi (high-water mark) ve görev stack yüksek su seviyesi raporlanabilmelidir. |
| **FR-66** | Kayıtlar, telemetri durdurulup devam eden TX tamamlandıktan **sonra** dışa aktarılmalıdır. |
| **FR-67** | t₄ değeri yanıt gönderildikten sonra bilindiği için, yanıt mesajının içine konulamaz; ayrı bir dışa aktarım adımı gereklidir. |
| **FR-68** | 32-bit sayaç farkları mod 2³² hesaplanmalıdır; ölçülen hiçbir aralık bir sayaç turundan uzun olmamalıdır. |
| **FR-69** | Ana ölçüm sırasında MCU sayaçları sürekli yazdırılmamalıdır (UART yükünü bozar). |

### 4.8 Kuyruk ve zamanlayıcı yapılandırması

| ID | Gereksinim |
|---|---|
| **FR-70** | `buttonQ` kapasitesi **8 olay** olmalıdır. |
| **FR-71** | `txQ` kapasitesi **16 mesaj**, disiplini FIFO olmalıdır. |
| **FR-72** | Zaman kaynağı çözünürlüğü **1 µs veya daha iyi** olmalıdır. |
| **FR-73** | Zaman kaynağı monoton olmalı ve ISR içinden güvenle okunabilmelidir. |
| **FR-74** | MCU saat frekansı, RTOS tick hızı, timer yapılandırması ve derleme optimizasyon seviyesi raporlanmalıdır. |

### 4.9 Komut arayüzü ve deney durum makinesi

> **Karar:** Senaryolar çalışma zamanında UART komutuyla değiştirilecektir (derleme zamanı `#define` yerine).
> **Kısıt:** FR-01 gereği uygulama görev sayısı 3'te kalmalıdır — komut işleme için **4. bir görev açılamaz**.

| ID | Gereksinim |
|---|---|
| **FR-80** | Komutlar UART RX üzerinden kesme ile alınmalıdır; polling kullanılmamalıdır. |
| **FR-81** | Komut alımı ve ayrıştırma, ek bir uygulama görevi açmadan gerçekleştirilmelidir (RX ISR + mevcut görevler). |
| **FR-82** | Komut satırları LF (`\n`) ile sonlandırılmalıdır. Komut mesajları 64 bayt dolgu kuralına tabi değildir. |
| **FR-83** | Tanınmayan, eksik veya taşan komutlar reddedilmeli, hata yanıtı üretilmeli ve sayılmalıdır; sessizce yutulmamalıdır. |
| **FR-84** | Sistem en az şu komutları desteklemelidir: senaryo seçme, deneyi başlatma/durdurma, kayıtları dışa aktarma, sayaç özetini isteme. |
| **FR-85** | Senaryo değişimi ancak bir deney **aktif değilken** kabul edilmelidir; koşan deneyin ortasında parametre değişmemelidir (MR-07). |
| **FR-86** | Senaryo değişiminde sistem: devam eden TX'i tamamlamalı, kayıt havuzunu ve tüm sayaçları sıfırlamalı, yeni parametreleri uygulamalıdır. |
| **FR-87** | Deney başlatıldığında 5 saniyelik ısınma uygulanmalı, ısınma boyunca telemetri çalışmalı ancak buton olayları kayda alınmamalıdır. |
| **FR-88** | Deney, o senaryo için hedeflenen kabul edilen basış sayısına (≥30) ulaşıldığında **otomatik olarak** sonlanmalıdır. |
| **FR-89** | Deney sonlandığında sistem telemetriyi durdurmalı, TX kuyruğunu boşaltmalı, ardından kayıtları dışa aktarmalıdır (FR-66). |
| **FR-90** | Deney durumu (boşta / ısınma / ölçüm / dışa aktarım) arayüze bildirilmeli ve kart üzerindeki LED'lerle görsel olarak ayırt edilebilmelidir. |
| **FR-91** | Komut işleme yolu, ana ölçüm sırasında UART TX hattına ek trafik eklememelidir; komut yanıtları yalnızca deney dışı anlarda üretilmelidir. |
| **FR-92** | UART RX kesmesinin NVIC önceliği, `FromISR` API'lerini çağırmaya uygun aralıkta olmalıdır (FR-17 ile aynı kural). |

---

## 5. PC arayüzü gereksinimleri (UI)

| ID | Gereksinim |
|---|---|
| **UI-01** | Arayüz mevcut seri portları listelemeli; bağlan ve bağlantıyı kes işlevi sunmalıdır. |
| **UI-02** | Gelen veri akışı LF ile çerçevelenmelidir. Seri okuma parçalı gelebilir; arayüz tampon tutup satır bazında ayrıştırmalıdır. |
| **UI-03** | Arayüz `TEL` ve `BTN` mesajlarını ayırt etmelidir. |
| **UI-04** | `TEL` ve `BTN` çerçevelerinin **64 bayt** olduğu doğrulanmalıdır; sapma sayılmalı ve gösterilmelidir. Komut yanıtı ve kayıt dökümü satırları bu kurala tabi değildir. |
| **UI-05** | `BTN` mesajı geldiğinde ekranda **"Butona basıldı"** ve ilgili `event_id` gösterilmelidir. |
| **UI-06** | Aktif senaryo, deney durumu (boşta / ısınma / ölçüm / dışa aktarım) ve toplanan olay sayısı ekranda görünmelidir. |
| **UI-07** | Arayüz, senaryo seçme / deney başlatma / durdurma / kayıt dışa aktarma komutlarını karta gönderebilmelidir (FR-84). |
| **UI-08** | Bir senaryo tamamlanıp kayıt dökümü alındığında arayüz **otomatik olarak** ilgili CSV dosyasını oluşturmalıdır (`S0.csv` … `S5.csv`). |
| **UI-09** | Altı senaryo tamamlandığında arayüz `summary.csv` özet dosyasını üretmelidir. |
| **UI-10** | Arayüz kayıp/hata sayaçlarını göstermelidir. |

### 5.1 Arayüzde grafik çizimi

| ID | Gereksinim |
|---|---|
| **UI-20** | Arayüz, `measurements/` altındaki CSV dosyalarını okuyup **grafik olarak çizebilmelidir**; çizim için harici bir araç gerekmemelidir. |
| **UI-21** | Kullanıcı hangi senaryo dosyalarının çizileceğini arayüzden seçebilmelidir (tek senaryo veya birden çok senaryo karşılaştırması). |
| **UI-22** | Arayüz AR-06'daki **iki zorunlu grafiği** üretebilmelidir: (a) olay numarası → R, 20 ms deadline çizgisiyle; (b) senaryo → aşama ortalamaları, yığılmış sütun. |
| **UI-23** | Grafikler arayüz penceresinin içine gömülü olarak görüntülenmelidir; ayrı bir pencere açılması zorunlu olmamalıdır. |
| **UI-24** | Her grafik PNG olarak `analysis/plots/` altına kaydedilebilmelidir. |
| **UI-25** | Çizim mantığı **tek bir modülde** toplanmalı ve hem arayüzden hem de bağımsız bir betikten çağrılabilmelidir (MR-08, AR-08). |
| **UI-26** | Grafikler yalnızca CSV dosyasındaki ham veriden üretilmelidir; canlı akıştan biriken bellek içi veriden değil. |
| **UI-27** | Çizimde `status != ok` olan satırlar dışlanmalı, dışlanan sayı grafik üzerinde belirtilmelidir (AR-07). |
| **UI-28** | Grafiklerde eksen birimleri (ms), örnek sayısı (n) ve senaryo kimliği görünmelidir (AR-07). |
| **UI-11** | Arayüz, MCU'nun zaman ölçümüne **müdahale etmemelidir**; PC saati ile MCU saati birbirinden çıkarılmamalıdır. |
| **UI-12** | Bozuk veya eksik çerçeveler sessizce yutulmamalı, sayılmalı ve gösterilmelidir. |
| **UI-13** | Ölçüm sürerken arayüz karta komut göndermemelidir (FR-91 ile tutarlı). |
| **UI-14** | Beklenen kayıt sayısı ile alınan kayıt sayısı karşılaştırılmalı; eksik varsa CSV yazılmadan önce uyarılmalıdır. |

---

## 6. Ölçüm ve deney gereksinimleri (MR)

### 6.1 Senaryolar

| ID | Telemetri | Ek CPU işi | Hedef |
|---|---|---|---|
| **S0** | Kapalı | Yok | Referans yanıt süresi |
| **S1** | 10 Hz · 100 ms | Yok | Düşük telemetri sıklığı |
| **S2** | 50 Hz · 20 ms | Yok | Orta telemetri sıklığı |
| **S3** | 100 Hz · 10 ms | Yok | Yüksek telemetri sıklığı |
| **S4** | 100 Hz · 10 ms | ≈2 ms | Ek CPU yükü (≈%20) |
| **S5** | 100 Hz · 10 ms | ≈5 ms | Daha yüksek CPU yükü (≈%50) |

| ID | Gereksinim |
|---|---|
| **MR-01** | Her senaryoda **en az 30 kabul edilen basış** toplanmalıdır. Toplam ≥ 180 olay. |
| **MR-02** | Basışlar arasında en az 0,5 saniye olmalı; basış zamanları değiştirilmelidir (düzenli ritim kullanılmamalıdır). |
| **MR-03** | Her senaryodan önce 5 saniye ısınma uygulanmalıdır. |
| **MR-04** | Senaryo değişiminde önceki TX tamamlanmalı, kayıtlar ve sayaçlar sıfırlanmalıdır. |
| **MR-05** | Her senaryo öncesi telemetri frekansı, 64 bayt mesaj boyu ve CPU işi süresi doğrulanmalıdır. |
| **MR-06** | Deney gözetim timeout'u 1 saniye olmalıdır (20 ms deadline'dan ayrıdır). |
| **MR-07** | Senaryolar karşılaştırılırken §4.8'deki sabitler (baud, mesaj boyu, kuyruk boyu, TX yöntemi, öncelik düzeni) **değiştirilmemelidir**. |
| **MR-08** | Ham CSV saklanmalı; tüm grafikler aynı ham veriden üretilmelidir. |
| **MR-09** | Kayıp ve hatalı olaylar raporda gizlenmemeli, ayrıca raporlanmalıdır. |
| **MR-10** | Ölçümler gerçek kart üzerinde alınmalıdır. Sentetik veri gerçek sonuç gibi sunulmamalıdır. |

### 6.2 CSV formatı

```
scenario,event_id,t0_us,t1_us,t2_us,t3_us,t4_us,status
S3,17,1000000,1002000,1002300,1004000,1009606,ok
S3,18,1500000,1502100,1502400,,,tx_drop
```

| ID | Gereksinim |
|---|---|
| **MR-20** | Her satır bir buton olayını temsil etmelidir. |
| **MR-21** | Alınamamış zaman damgaları **boş bırakılmalıdır**; 0 yazılmamalıdır. |

---

## 7. Analiz gereksinimleri (AR)

| ID | Gereksinim |
|---|---|
| **AR-01** | Her senaryo için başarılı ölçüm sayısı raporlanmalıdır. |
| **AR-02** | R için minimum, ortalama ve **gözlenen maksimum** raporlanmalıdır. Yalnızca ortalama yeterli değildir. |
| **AR-03** | 20 ms'yi aşan **tamamlanmış** yanıt sayısı raporlanmalıdır. Kayıp yanıtlar "deadline karşılandı" sayılmamalıdır. |
| **AR-04** | Drop, TX hatası, timeout ve kayıt kaybı sayıları raporlanmalıdır. |
| **AR-05** | Aşama süreleri (`t₁−t₀`, `t₂−t₁`, `t₃−t₂`, `t₄−t₃`) senaryolar arasında karşılaştırılmalıdır. |
| **AR-06** | **En az 2 grafik** üretilmelidir: (a) olay numarası → R, 20 ms deadline çizgisiyle; (b) senaryo → aşamaların ortalama süreleri, yığılmış sütun. |
| **AR-07** | Grafiklerde birim, örnek sayısı ve dışlanan kayıtlar belirtilmelidir. |
| **AR-08** | Grafik üretme kodu depoya eklenmelidir. |
| **AR-09** | Rapor şu dört soruyu yanıtlamalıdır: Hangi bileşen değişti? Neden? Hangi ölçüm destekliyor? Ne henüz bilinmiyor? |
| **AR-10** | Gözlenen maksimum, kanıtlanmış worst-case olarak sunulmamalıdır. |
| **AR-11** | Özet tabloda gerçek telemetri hızı ve kuyruk yüksek su seviyesi de yer almalıdır. |
| **AR-12** | Senaryolar arasında anlamlı fark çıkmaması başarısızlık değildir; ancak açıklanmalıdır. |
| **AR-13** | CPU süresi ile duvar saati (wall clock) aralığı arasındaki fark raporda açıklanmalıdır. |

---

## 7b. Kod dokümantasyonu gereksinimleri (DOC)

> **Karar:** Firmware kaynak kodu Doxygen ile belgelenecek ve Doxygen çıktısı üretilecektir.
> Bu, DR-04 (`code-notes.md`) ile çelişmez; aksine onun ham maddesini oluşturur: Doxygen **her sembolü** belgeler, `code-notes.md` ise **kritik kod bloklarını** anlatır.

| ID | Gereksinim |
|---|---|
| **DOC-01** | Her C kaynak ve başlık dosyası, `@file` ve `@brief` içeren bir dosya başlığı bloğuyla başlamalıdır. |
| **DOC-02** | Tüm public fonksiyonlar `@brief`, varsa `@param` ve `@return` ile belgelenmelidir. |
| **DOC-03** | Tüm public tip, enum sabiti, makro ve global değişken belgelenmelidir. |
| **DOC-04** | ISR'ler ve ISR'den çağrılan fonksiyonlar `@note` ile **çağrı bağlamını** (ISR / görev) açıkça belirtmelidir. |
| **DOC-05** | Zaman damgası alan veya kayıt alanı yazan her fonksiyon, hangi damgayı (t₀…t₄) hangi gereksinim maddesi uyarınca aldığını belgelemelidir. |
| **DOC-06** | Paylaşılan değişkenler için erişim kuralı belgelenmelidir: kim yazar, kim okur, hangi bağlamda (bkz. `02-design.md` §3.1). |
| **DOC-07** | Depoda bir `Doxyfile` bulunmalıdır; varsayılan ayarlarla değil, projeye göre yapılandırılmış olmalıdır. |
| **DOC-08** | Doxygen üretimi **sıfır uyarı** ile tamamlanmalıdır (`WARN_IF_UNDOCUMENTED = YES`). |
| **DOC-09** | Kod modülleri `@defgroup` / `@ingroup` ile mantıksal gruplara ayrılmalıdır (ISR, görevler, kayıt defteri, protokol, durum makinesi). |
| **DOC-10** | Üretilen Doxygen çıktısı depoya eklenmelidir; eğitmenin Doxygen kurmadan inceleyebilmesi gerekir. |
| **DOC-11** | Doxygen üretme komutu `README.md` içinde yer almalıdır. |
| **DOC-12** | Python kaynakları docstring ile belgelenmelidir; Doxygen kapsamı zorunlu değildir. |

---

## 8. Teslim gereksinimleri (DR)

```
freertos-bootcamp/
└── hafta-01/
    ├── README.md
    ├── firmware/          ← FreeRTOSConfig.h dahil tüm kaynak
    ├── interface/
    ├── measurements/
    │   ├── S0.csv … S5.csv
    │   └── summary.csv
    ├── analysis/
    │   ├── report.md
    │   └── plots/
    └── docs/
        ├── setup.md
        ├── code-notes.md
        ├── ai-usage.md
        ├── Doxyfile
        └── doxygen/            ← üretilen çıktı (DOC-10)
```

| ID | Gereksinim |
|---|---|
| **DR-01** | Tek bir GitHub deposunda teslim edilmelidir; eğitmenin erişimi olmalıdır (özel depoysa erişim önceden verilmelidir). |
| **DR-02** | Teslim commit SHA'sı paylaşılmalıdır. |
| **DR-03** | `README.md`: kart, bağlantılar, araç sürümleri, derleme/yükleme adımları, arayüzü başlatma, senaryo seçimi, timer ve FreeRTOS ayarları, ham veri/grafik/rapor bağlantıları. |
| **DR-04** | `code-notes.md`: ISR, görevler, UART tamamlanması ve zaman hesapları **kod bloklarıyla** açıklanmalıdır. Yalnızca ekran görüntüsü kabul edilmez. |
| **DR-05** | `ai-usage.md`: hangi işlerde AI desteği alındı, üretilen kod nasıl kontrol edildi, hangi öneriler değiştirildi ve neden. |
| **DR-06** | Kaynak dosyalar bulunmalıdır; üretilmiş binary tek başına yeterli değildir. |
| **DR-07** | Telegram'a depo bağlantısı ve anlatım videosu gönderilmelidir. |
| **DR-08** | Video: fiziksel basış, arayüz yanıtı, frekans değişimi ve ölçüm yorumu yer almalıdır. |
| **DR-09** | Teslim **27 Eylül 2026 Pazar 11.00**'den önce tamamlanmalıdır. |

---

## 9. Kabul kriterleri (teslim öncesi kontrol)

- [ ] Firmware ve arayüz kaynakları çalıştırılabilir durumda.
- [ ] S0–S5 ham ölçümleri, kayıp sayaçları ve özet tablo depoda.
- [ ] Grafikler ve analiz raporu ham veriye dayanıyor.
- [ ] Kritik kod blokları açıklanmış; AI kullanımı belirtilmiş.
- [ ] Depo erişimi, çalıştırma adımları ve teslim commit'i hazır.
- [ ] Videoda çalışan sistem ve senaryoların farkı gösterilmiş.

---

## 10. Kararlar (çözülmüş açık sorular)

| ID | Soru | Karar | Tarih |
|---|---|---|---|
| **S-1** | Kart modeli ve seri bağlantı yöntemi | **STM32F4DISCOVERY** (ST-LINK/V2-1, PID 374B); ölçüm hattı **FT232RL USB-TTL, COM7** (§3). T-03'te doğrulandı. | 20.09.2026 |
| **S-2** | Buton pini ve aktif seviyesi | **B1 USER (mavi), PA0, aktif-HIGH** — T-03'te ölçüldü: boşta 0, basılı 1. Donanım debounce yok; 6 basışta 9 kenar ölçüldü → yazılım filtresi zorunlu. | 21.09.2026 |
| **S-3** | Senaryo seçimi | **Çalışma zamanında UART komutu.** 4. görev açılmadan çözülecek (§4.9) | 20.09.2026 |
| **S-4** | Kayıt dışa aktarımı | Hedef basış sayısına ulaşınca **otomatik**; döküm alınınca arayüz CSV'yi kendiliğinden yazar (FR-88, UI-08) | 20.09.2026 |
| **S-5** | Depo yapısı | Mevcut dizin git deposuna çevrilecek; ders dosyaları `ders-materyali/`, teslim `hafta-01/` altında | 20.09.2026 |

### Şartnameden sapmalar

| Sapma | Gerekçe | Nereye yazılacak |
|---|---|---|
| Senaryo seçimi derleme zamanı yerine çalışma zamanı UART komutuyla yapılıyor | Şartname ek komut görevini *zorunlu değil* sayar, yasaklamaz. Görev sayısı 3'te tutularak FR-01 korunuyor. Senaryolar arası geçiş hızlanıyor, deney parametreleri tek binary'de sabit kalıyor. | `README.md` |

---

## 10b. Kalan riskler

| ID | Risk | Azaltma |
|---|---|---|
| **R-1** | RX kesme yolu ölçüme ek kesme yükü getirebilir | FR-91: ölçüm sırasında komut trafiği yok; RX yükü S0 referansıyla karşılaştırılarak raporlanır |
| **R-2** | HAL'in `TxCpltCallback` yolu gerçek TC yerine yalnızca DMA tamamlanmasını gösterebilir | Şartname bunu açıkça uyarıyor; sürücü kodu okunarak TC yolu doğrulanacak, bulgu `code-notes.md`'ye yazılacak |
| **R-3** | PA0 aynı zamanda WKUP pini; beklenmedik yan etki olabilir | HW-04 doğrulaması sırasında birlikte test edilecek |
| **R-4** | Bir haftalık süre; komut yolu kapsamı büyüttü | Task listesinde ölçüm alma adımı öne çekilecek, süsleme işleri sona bırakılacak |

---

## 11. İzlenebilirlik

Tüm gereksinimler `odev-01.html` şartnamesinden türetilmiştir. Şartnameden **sapma olursa** gerekçesi `README.md` içinde yazılmalıdır (şartname bunu açıkça izin veriyor).

---

## Onay

- [ ] Kullanıcı bu gereksinimleri okudu ve onayladı.
- [ ] Açık sorular (§10) yanıtlandı.
- [ ] `02-design.md` yazımına geçilebilir.
