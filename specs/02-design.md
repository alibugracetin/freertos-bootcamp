# 02 — Tasarım (Design)

**Girdi:** `01-requirements.md`
**Durum:** 🟡 TASLAK — kullanıcı onayı bekliyor

> Bu dosya **nasıl** yapılacağını, her kararın **gerekçesiyle** birlikte tanımlar.
> `hafta-01/docs/code-notes.md` büyük ölçüde bu dosyadan türetilecektir.

---

## 1. Sistem mimarisi

```
   ┌──────────────┐
   │ B1 (PA0)     │  fiziksel basış
   └──────┬───────┘
          │ yükselen kenar
   ┌──────▼──────────────────┐
   │ EXTI0_IRQHandler        │  t₀ = TIM2->CNT
   │ · debounce (30 ms)      │  event_id ata
   │ · kayıt aç              │
   └──────┬──────────────────┘
          │ xQueueSendFromISR
   ┌──────▼──────────┐
   │ buttonQ  (8)    │
   └──────┬──────────┘
          │
   ┌──────▼─────────────────────┐        ┌──────────────────────────┐
   │ ButtonTask   (öncelik 2)   │        │ TelemetryTask (öncelik 3)│
   │ · t₁ kaydet                │        │ · calibrated_work()      │
   │ · yanıt mesajı hazırla     │        │ · TEL mesajı üret        │
   │ · t₂ kaydet                │        │ · xTaskDelayUntil        │
   │ · DENEY DURUM MAKİNESİ     │        └───────────┬──────────────┘
   └──────┬─────────────────────┘                    │
          │            xQueueSend                    │
          └──────────────┬───────────────────────────┘
                  ┌──────▼──────────┐
                  │ txQ  (16, FIFO) │
                  └──────┬──────────┘
                         │
                  ┌──────▼─────────────────────┐
                  │ UartTxTask   (öncelik 1)   │
                  │ · t₃ kaydet                │
                  │ · HAL_UART_Transmit_DMA    │
                  │ · TC'ye kadar blokla       │
                  └──────┬─────────────────────┘
                         │
            ┌────────────▼─────────────┐      ┌───────────────────────┐
            │ USART2_IRQHandler (TC)   │      │ USART2_IRQHandler (RX)│
            │ · t₄ kaydet              │      │ · komut satırı topla  │
            │ · görevi uyandır         │      │ · LF'de ayrıştır      │
            └────────────┬─────────────┘      └───────────┬───────────┘
                         │                                │
                  ══════ UART 115200 8N1 ══════════════════
                         │  FT232RL                       ▲
                  ┌──────▼─────────────────────────────────┴──────┐
                  │  PC arayüzü (Python)                          │
                  │  okuma thread'i → ayrıştırıcı → GUI → CSV     │
                  └───────────────────────────────────────────────┘
```

**Temel kural (FR-05):** Ölçüm zincirindeki hiçbir görev UART'a doğrudan dokunmaz. Yalnızca `UartTxTask` dokunur. Bu, `t₃−t₂` aralığının anlamını korur: o aralık saf "TX kuyruğunda sıra bekleme + başlatma" süresidir.

---

## 2. Donanım eşlemesi

| Kaynak | Seçim | Gerekçe |
|---|---|---|
| Buton | **PA0 / EXTI0**, yükselen kenar | Kart üzerindeki B1 USER, aktif-HIGH (HW-04 ile doğrulanacak) |
| Zaman kaynağı | **TIM2**, 32-bit, 1 MHz | F407'de TIM2 ve TIM5 32-bit; 1 MHz'de ≈71.6 dk sarma (FR-72) |
| UART | **USART2**, PA2 = TX, PA3 = RX | Discovery'de bu pinler boşta (HW-03 ile doğrulanacak) |
| TX yolu | **DMA1 Stream6, Kanal 4** | Bayt başına kesme yok → ölçümü bozan CPU yükü minimum |
| RX yolu | **Kesme (IT)**, DMA yok | Komut trafiği seyrek; bayt başına kesme kabul edilebilir |
| HAL timebase | **TIM6** | SysTick FreeRTOS'a ait; HAL ayrı bir timer kullanmalı |
| Durum LED'leri | PD12 yeşil, PD13 turuncu, PD14 kırmızı, PD15 mavi | FR-90 görsel durum |

### 2.1 TIM2 yapılandırması

```
APB1 timer saati = 84 MHz   (SYSCLK 168 MHz, APB1 böleni 4 → 42 MHz, timer ×2 → 84 MHz)

PSC = 84 − 1 = 83     →  84 MHz / 84 = 1 MHz  →  1 tick = 1 µs
ARR = 0xFFFFFFFF      →  serbest sayım, kesme yok
```

Okuma:

```c
static inline uint32_t timer_us(void) { return TIM2->CNT; }
```

> ⚠️ **Başlatma şart:** CubeMX `MX_TIM2_Init()` timer'ı yapılandırır ama **başlatmaz**. `HAL_TIM_Base_Start(&htim2)` çağrılmadan `TIM2->CNT` sıfırda sabit kalır ve tüm zaman damgaları 0 çıkar. Ölçüm firmware'inde bu çağrı scheduler başlamadan önce yapılmalıdır.

**Doğrulama (T-04, 21.09.2026):** Aynı 1 s'lik aralık iki bağımsız sayaçla ölçüldü — TIM2 (APB1 timer saati, ÷84) ve DWT->CYCCNT (HCLK 168 MHz). Üç turda fark **1 µs** (tam sayı bölmesi yuvarlaması), yani TIM2 ile çekirdek saati 1 ppm içinde eşleşiyor. Ön-bölücü hatası olsaydı fark ≈12 000 µs/s olurdu. Kanıt: `hafta-01/docs/kanitlar/t03-t04-hwtest.log`.

**Neden kesmesiz ve HAL'siz?** Tek bir hizalı 32-bit register okuması Cortex-M4'te atomiktir — kilit gerekmez, ISR içinden güvenle çağrılabilir (FR-73). HAL sarmalayıcısı ekstra çağrı yükü getirir; ölçüm noktalarında bu yükü istemiyoruz.

**Sarma (FR-68):** Tüm farklar `uint32_t` aritmetiğiyle hesaplanır; işaretsiz çıkarma mod 2³² doğal olarak doğru sonucu verir:

```c
uint32_t delta = t_end - t_start;   /* sarma olsa bile doğru, aralık < 71.6 dk ise */
```

### 2.2 NVIC öncelik planı

FreeRTOS + Cortex-M4'te **kritik kural**: `...FromISR()` çağıran her kesmenin sayısal önceliği `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` değerinden **büyük veya eşit** olmalıdır. Cortex-M'de **büyük sayı = düşük öncelik**.

| Kesme | Preemption önceliği | FromISR çağırıyor mu? | Gerekçe |
|---|---|---|---|
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | **5** | — | CubeMX varsayılanı |
| **EXTI0** (buton) | **5** | ✅ Evet | Kurala uygun en yüksek izinli seviye → t₀ gecikmesi minimum |
| **USART2** (TC + RX) | **5** | ✅ Evet | EXTI0 ile **eşit** öncelik |
| `SysTick` / `PendSV` | 15 | — | FreeRTOS tarafından yönetilir |
| DMA1_Stream6 | 5 | ❌ Hayır | TC yolunu USART2 kesmesine devreder |

**Neden EXTI0 ve USART2 eşit öncelikte?** Eşit önceliğe sahip kesmeler birbirini kesemez; sıraya girerler. Böylece bir buton basışı, önceki olayın t₄ ölçümünü **bölemez** — ve tersi de geçerlidir. İki ISR de birkaç mikrosaniye sürdüğü ve basışlar arası en az 500 ms olduğu için (MR-02) kuyruklanma gecikmesi ihmal edilebilir, ama **öngörülebilir** olur. Farklı öncelikler verseydik, ölçüm noktalarından biri diğerinin lehine sistematik olarak sapardı.

> ⚠️ `HAL_Init()` öncelik gruplamasını `NVIC_PRIORITYGROUP_4` yapar (4 bit preemption, 0 bit subpriority). Bu plan buna dayanır; değiştirilmemelidir.

---

## 3. Veri yapıları

```c
/* ---- ISR'den göreve taşınan buton olayı ---- */
typedef struct {
    uint32_t event_id;
    uint32_t t0_us;
} ButtonEvent;                      /* 8 bayt, kuyruğa değerle kopyalanır */

/* ---- TX kuyruğundaki mesaj ---- */
typedef enum { MSG_TEL, MSG_BTN, MSG_REC, MSG_ACK } MsgKind;

typedef struct {
    MsgKind  kind;
    uint32_t event_id;              /* MSG_BTN için: t₃/t₄ bu kaydı kapatacak */
    uint16_t len;                   /* gönderilecek bayt sayısı */
    char     data[96];              /* TEL/BTN tam 64 bayt kullanır; REC en fazla 83 */
} TxMsg;                            /* ≈108 bayt; txQ = 16 × 108 ≈ 1.7 KB */

/* ---- Ölçüm kaydı ---- */
typedef enum { ST_OPEN, ST_OK, ST_BTNQ_DROP, ST_TXQ_DROP,
               ST_TX_ERROR, ST_TIMEOUT } RecStatus;

typedef struct {
    uint32_t  event_id;
    uint32_t  t[5];                 /* t0..t4 */
    uint8_t   have;                 /* bit maskesi: hangi damgalar geçerli */
    RecStatus status;
} EventRecord;

#define REC_POOL_SIZE 64            /* FR-60 */
static EventRecord g_rec[REC_POOL_SIZE];
```

**Tampon neden 96 bayt? (düzeltme, 22.09.2026)** İlk taslakta 64'tü. Ancak `REC` döküm satırının en kötü durumu: `REC,` (4) + `S5,` (3) + 10 haneli kimlik ve virgül (11) + 5 × (10 hane + virgül) (55) + `btnq_drop` (9) + LF (1) = **83 bayt**. 64 baytlık tampon bu satırı keserdi. TEL/BTN yine tam 64 bayt gönderir (FR-50); fazladan alan yalnızca deney dışındaki döküm için kullanılır.

**Slot kuralı:** Bir slot yalnızca `REC_FREE` durumundayken açılabilir. Açık **veya kapanmış ama henüz dökülmemiş** bir kaydın üzerine yazılmaz; bu durumda `rec_ovf` artar ve yeni olay ölçülmez (FR-61). Havuz yalnızca senaryo değişiminde (`IDLE`) sıfırlanır.

**t₀ yakalama noktası:** CubeMX'in ürettiği `EXTI0_IRQHandler()` içinde `HAL_GPIO_EXTI_IRQHandler()` çağrısından önceki USER CODE bloğunda `button_irq_entry()` çağrılır ve timer okunur. HAL daha sonra bayrağı temizleyip `HAL_GPIO_EXTI_Callback()`'i çağırır; filtre ve kuyruk işlemleri orada, yakalanmış t₀ ile yapılır. Aynı kesme çağrısı içinde yazılıp okunduğu için bu ara değişken yarış oluşturmaz.

**`have` bit maskesi neden gerekli?** MR-21 alınamamış zaman damgalarının CSV'de **boş** bırakılmasını, 0 yazılmamasını istiyor. `t[i] == 0` ile "0 mikrosaniyede alındı" ayırt edilemez — sayaç gerçekten 0'dan geçebilir. Ayrı bir geçerlilik biti bu belirsizliği kaldırır.

**Kayıt–olay eşleşmesi (FR-46):** `event_id % REC_POOL_SIZE` ile slot bulunur. Slot hâlâ eski bir olayla doluysa taşma sayacı artırılır (FR-61). Her slot yazılmadan önce `event_id` doğrulanır — böylece geç gelen bir TC kesmesi yanlış kaydı bozamaz.

### 3.1 Kayıt yaşam döngüsü ve eşzamanlılık (FR-62)

Bir kaydın alanları **beş ayrı bağlamdan** yazılır:

| Alan | Yazan bağlam | Sıra |
|---|---|---|
| `t[0]`, slot açılışı | EXTI0 ISR | 1 |
| `t[1]`, `t[2]` | `ButtonTask` | 2 |
| `t[3]` | `UartTxTask` | 3 |
| `t[4]`, `status` | USART2 TC ISR | 4 |

**Neden kilit gerekmiyor?** Bu dört aşama aynı olay için **kesin olarak sıralıdır**: t₁ ancak olay kuyruktan alınınca, t₃ ancak mesaj TX kuyruğundan çekilince, t₄ ancak DMA başlatılınca oluşabilir. Aynı kaydın iki alanına asla aynı anda yazılmaz. Farklı olaylar farklı slotlara düşer, dolayısıyla bellekte çakışmazlar.

**Tek gerçek yarış: paylaşılan sayaçlar.** `ovf++` gibi read-modify-write işlemleri hem ISR'den hem görevden çağrılırsa atomik değildir ve sayım kaybolabilir.

**Karar:** Kritik bölüm kullanmak yerine **ISR sayaçları ile görev sayaçları ayrı değişkenlerde tutulur**; yalnızca döküm anında (tek bağlamda, deney bittikten sonra) toplanır.

```c
static volatile uint32_t g_ovf_isr;    /* yalnızca ISR yazar  */
static          uint32_t g_ovf_task;   /* yalnızca görev yazar */
/* döküm anında: total = g_ovf_isr + g_ovf_task; */
```

Bu, ölçüm yolundan `portENTER_CRITICAL()` çağrılarını tamamen kaldırır — kesmeleri kapatmak t₀ ve t₄'ün doğruluğunu bozabilirdi ve FR-24 zaten kesme kapatmayı yasaklıyor.

### 3.2 Ölçümün iki akışı

| Akış | Ne zaman | Amaç | Ölçümü etkiler mi? |
|---|---|---|---|
| **Canlı** — `TEL` / `BTN` | Deney sırasında | Arayüzde anlık geri bildirim; **deneyin yükünü bu üretir** | Evet, kasıtlı |
| **Döküm** — `REC` / `CNT` | `DONE` durumunda, telemetri kapalıyken | Asıl ölçüm verisinin dışa aktarımı | Hayır (FR-66, FR-91) |

t₄ ancak yanıt gönderildikten sonra bilindiği için ölçüm verisi canlı akışa konulamaz (FR-67). İki akışın ayrılması bu kısıtın doğrudan sonucudur.

---

## 4. ISR tasarımları

### 4.1 EXTI0 — buton

```
EXTI0_IRQHandler:
    now = TIM2->CNT                          ← t₀ EN BAŞTA okunur (FR-11)
    EXTI bayrağını temizle
    if (!first_event && (now - last_accept) < 30000 µs):   ← FR-12
        debounce_reject++ ; return                          ← FR-13: ilk olay muaf
    last_accept = now ; first_event = false
    if (deney ölçüm durumunda değil):                       ← FR-87 ısınma
        return
    id = ++event_counter                                    ← FR-14
    slot aç, t₀ yaz, status = ST_OPEN
    e = { id, now }
    if (xQueueSendFromISR(buttonQ, &e, &wake) != pdPASS):   ← FR-15
        btnq_drop++ ; slot.status = ST_BTNQ_DROP            ← FR-18
    portYIELD_FROM_ISR(wake)
```

**Neden bayrak temizlemeden önce timer okunuyor?** Bayrak temizleme bir periferik yazması; F4'te bu yazmanın tamamlanması birkaç çevrim sürebilir. Timer'ı önce okumak t₀'ı kenara en yakın ana sabitler. Şartname de t₀'ı "ISR girişinde" diye tanımlıyor.

**Neden ISR içinde 30 ms filtresi?** Discovery'nin mavi butonunda **donanım debounce yok**. Filtre göreve bırakılsaydı, zıplama sırasındaki her kenar `buttonQ`'ya bir olay yazar, 8 elemanlı kuyruğu tek basışta doldurabilirdi.

**Ölçülmüş kanıt (T-03, 21.09.2026):** Filtresiz EXTI sayacıyla 6 basışta **9 yükselen kenar** sayıldı; bir basış 2, bir basış 3 kenar üretti. Filtre varsayım değil, ölçülmüş bir ihtiyaçtır.

### 4.1.1 Bırakış zıplaması ve yeniden silahlanma (FR-12b) — 22.09.2026

**Bulgu (T-07):** Kullanıcı 5 kez bastı (ikisi uzun basış); sistem **8** olay kabul etti. Kayıt defterindeki t₀ zaman çizelgesi, fazladan üç olayın önceki basıştan 176 ms, 1 318 ms ve 1 240 ms sonra geldiğini gösterdi: bunlar **bırakış anındaki zıplamanın** yükselen kenarlarıdır. FR-12'nin 30 ms penceresi yalnızca kabul edilen kenardan sonraki 30 ms'yi korur; bırakış yüzlerce milisaniye sonra gelir ve pencerenin dışında kalır.

**Neden önemli:** Hayalet olaylar gerçek bir yanıt ve geçerli görünen bir R üretir. Veride ayırt edilemezler; "30 basış" dediğimiz örneklemin bir kısmı insan basışı olmaz.

**Çözüm:** Kesme yalnızca yükselen kenarda kalır ("sadece basış kenarı" korunur). Buna bir **silah** bayrağı eklenir:

```
ISR (yükselen kenar):
    son kabulden < 30 ms     → debounce_rej++   (basış zıplaması, FR-12)
    silah kapalı             → unarmed_rej++    (bırakış zıplaması / basılı tutma, FR-12b)
    aksi halde               → KABUL, silahı kapat

ButtonTask (≤ 50 ms'de bir):
    pin LOW ve ilk LOW gözleminden beri ≥ 30 ms geçti → silahı aç
```

- **Başlangıçta silah açıktır** — ilk basış filtrelenmez (FR-13).
- **Silahı açma** kritik bölüm içinde, pin yeniden okunarak yapılır: görev pini LOW okuyup bayrağı açana kadar geçen mikrosaniyelerde bir basış kenarı gelirse, EXTI bekleyen kesme olarak kilitlenir ve kritik bölümden çıkınca silah açık bulunarak kabul edilir. Kritik bölüm yalnızca birkaç komut sürer ve ölçüm yolunda değildir.
- **Maliyet:** ButtonTask 50 ms'de bir uyanır ve bir pin okur (~1 µs). Bu uyanma §7'deki deney durum makinesi için zaten planlıydı.
- **Sınır:** 30 ms'den kısa süren bırakışlar (basış–bırakış–basış) tek basış sayılır. MR-02 basışlar arasında ≥ 0,5 s istediği için ölçüm senaryolarında oluşmaz.

**Neden t₀ yoklamayla alınmaz?** Aynı testte 5 ms'lik yoklama döngüsünün aldığı basış zamanlarının hepsi `…954` µs ile bitti: döngü hep aynı milisaniye fazında uyandığı için damga, basış anını değil *döngünün basışı fark ettiği anı* (0–5 ms gecikmeyle) gösteriyordu. t₀'ın ISR girişinde alınması (FR-11) bu nicemleme gürültüsünü ortadan kaldırır.

### 4.2 USART2 — TC (t₄)

HAL akışı: `HAL_UART_Transmit_DMA` → DMA transfer tamamlanınca HAL, USART **TC** kesmesini açar → TC bayrağında `HAL_UART_TxCpltCallback` çağrılır.

```
HAL_UART_TxCpltCallback:
    now = TIM2->CNT                          ← t₄
    if (gönderilen mesaj MSG_BTN idi):
        kaydı bul (event_id) ; t₄ yaz ; status = ST_OK
    vTaskNotifyGiveFromISR(UartTxTask)       ← FR-43
```

> ⚠️ **Risk R-2 burada doğrulanacak.** Şartname "DMA bitti ≠ son bit çıktı" uyarısı yapıyor. Bu tasarım, HAL'in `TxCpltCallback`'i **DMA tamamlanmasında değil, USART TC bayrağında** çağırdığı varsayımına dayanır. Kullanılan HAL sürümünde `UART_DMATransmitCplt` ve `UART_EndTransmit_IT` fonksiyonları okunarak doğrulanacak; bulgu `code-notes.md`'ye yazılacaktır. Varsayım yanlış çıkarsa t₄ bir mesaj süresi (≈5.56 ms) erken ölçülmüş olur — bu, tüm sonuçları sistematik olarak kaydırır.

### 4.3 USART2 — RX (komutlar)

```
HAL_UART_RxCpltCallback:                     ← bayt bayt, FR-80
    c = rx_byte
    if (c == '\n'):
        cmd_ready = true ; satırı işle
    else if (cmd_len < CMD_MAX):
        cmd_buf[cmd_len++] = c
    else:
        cmd_overflow++ ; cmd_len = 0         ← FR-83
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1)   /* bir sonraki baytı iste */
```

Komut **ayrıştırması** ISR içinde yapılmaz; yalnızca satır toplanır ve bir bayrak set edilir. Ayrıştırmayı `ButtonTask` housekeeping turunda yapar (§7) — böylece ISR kısa kalır (FR-16 ruhu) ve **4. görev açılmaz** (FR-81).

---

## 5. Görev tasarımları

| Görev | Öncelik | Stack | Bloklama noktası |
|---|---|---|---|
| `TelemetryTask` | 3 (yüksek) | 512 word | `xTaskDelayUntil` / S0'da `ulTaskNotifyTake` |
| `ButtonTask` | 2 (orta) | 512 word | `xQueueReceive(buttonQ, …, 50 ms)` |
| `UartTxTask` | 1 (düşük) | 512 word | `xQueueReceive(txQ, …, portMAX_DELAY)` + `ulTaskNotifyTake` |

> ⚠️ **CubeMX tuzağı:** CMSIS-RTOS v2 etkinleştirildiğinde CubeMX otomatik olarak bir `defaultTask` üretir. **Bu görev silinmelidir**, aksi halde uygulama görev sayısı 4 olur ve FR-01 ihlal edilir.
>
> ⚠️ CMSIS-RTOS v2'de `stack_size` **bayt** cinsindendir; native FreeRTOS `xTaskCreate`'te ise **word** cinsindendir. 512 word = 2048 bayt.

### 5.1 TelemetryTask

```c
for (;;) {
    if (scenario == S0) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* FR-22: gerçekten bloklu */
        last = xTaskGetTickCount();
        continue;
    }
    calibrated_work(g_iterations);                 /* FR-23, S4/S5'te > 0 */
    TxMsg m = make_telemetry();                    /* 64 bayt, FR-50 */
    if (xQueueSend(txQ, &m, 0) != pdPASS)          /* FR-26: bloklamaz */
        tel_drop++;
    xTaskDelayUntil(&last, g_period_ticks);        /* FR-20 */
}
```

**S0'da neden notification, neden `vTaskDelay` değil?** FR-22 görevin gerçekten bloklanmasını istiyor. `ulTaskNotifyTake(portMAX_DELAY)` ile görev ready listesinden tamamen çıkar — CPU tüketimi **sıfır**. Periyodik uyanma olsaydı S0 referansı, ölçmek istemediğimiz bir yükü içerirdi.

**`calibrated_work` neden optimize edilemez?**

```c
static volatile uint32_t g_work_sink;

static void calibrated_work(uint32_t iters) {
    uint32_t acc = 0x12345678u;
    for (uint32_t i = 0; i < iters; i++)
        acc = acc * 1664525u + 1013904223u;   /* LCG: her adım öncekine bağlı */
    g_work_sink = acc;                        /* volatile → sonuç kullanılıyor */
}
```

Her iterasyon bir öncekinin sonucuna bağlı olduğu için derleyici döngüyü açamaz veya paralelleştiremez; `volatile` yazma nedeniyle tamamen silemez (FR-23). `vTaskDelay` **kullanılmaz** — o CPU yükü üretmez, tam tersine CPU'yu bırakır (FR-24).

**Kalibrasyon (FR-25):** `iters` değeri karta göre ölçülerek bulunur — `timer_us()` ile 1000 tekrarın süresi ölçülür, 2 ms ve 5 ms hedeflerine göre ölçeklenir. Ölçülen gerçek süre raporlanır; duvar saati ölçümü kesme/preemption içerebileceği için bu fark `report.md`'de açıklanır (AR-13).

### 5.2 ButtonTask

```c
for (;;) {
    if (xQueueReceive(buttonQ, &e, pdMS_TO_TICKS(50)) == pdPASS) {
        record_t(e.event_id, 1, timer_us());       /* t₁ — FR-31 */
        TxMsg m = make_button_reply(e.event_id);   /* 64 bayt */
        record_t(e.event_id, 2, timer_us());       /* t₂ — FR-32 */
        if (xQueueSend(txQ, &m, 0) != pdPASS) {
            txq_drop_btn++;
            close_record(e.event_id, ST_TXQ_DROP); /* FR-34 */
        }
        accepted_count++;
    }
    experiment_tick();                             /* §7 durum makinesi */
}
```

**`t₂` neden `xQueueSend`'den hemen önce?** `t₂−t₁` yalnızca "yanıtı hazırlama" süresini ölçmeli. Çağrıdan sonra alınsaydı, kuyruk API'sinin kendi süresi de hazırlamaya yazılırdı. Buna karşılık `t₃−t₂` **saf FIFO beklemesi değildir** — kuyruk API'si, scheduling ve UART hazırlığı da o aralığın içindedir. Bu ayrım `report.md`'de açıkça yazılacaktır.

**50 ms timeout neden ölçümü bozmaz?** `experiment_tick()` yalnızca birkaç sayaç karşılaştırması yapar (~µs). Buton olayı geldiğinde `xQueueReceive` zaten hemen döner; timeout yolu sadece **olay yokken** çalışır. Basışlar arası en az 500 ms olduğu için (MR-02) çakışma penceresi ihmal edilebilir, ve bu seçim `README`'de gerekçelendirilecektir.

### 5.3 UartTxTask

```c
for (;;) {
    xQueueReceive(txQ, &m, portMAX_DELAY);         /* FR-40 */
    memcpy(g_tx_buf, m.data, m.len);               /* HW-05: normal SRAM */
    g_tx_kind = m.kind; g_tx_event = m.event_id;   /* TC callback için */
    if (m.kind == MSG_BTN)
        record_t(m.event_id, 3, timer_us());       /* t₃ — FR-42 */
    if (HAL_UART_Transmit_DMA(&huart2, g_tx_buf, m.len) != HAL_OK) {
        uart_err++;
        close_record(m.event_id, ST_TX_ERROR);     /* FR-44: sonsuz bekleme yok */
        continue;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
        uart_timeout++;                            /* MR-06: 1 s gözetim */
        HAL_UART_AbortTransmit(&huart2);
        close_record(m.event_id, ST_TIMEOUT);
    }
}
```

**`g_tx_buf` neden `TxMsg` içindeki dizi yerine ayrı bir global?** FR-45 tamponun aktarım sonuna kadar geçerli kalmasını istiyor. `m` görev stack'inde bir yerel değişkendir; DMA sürerken görev başka bir şey yapıp o stack alanını geçersizleştirebilir. Kalıcı bir global tampon bu riski ortadan kaldırır. `memcpy` maliyeti 64 bayt — ihmal edilebilir.

**Tek uçuş kuralı:** Aynı anda yalnızca bir DMA aktarımı vardır; görev TC'yi bekleyerek bloklanır. Bu nedenle tek bir `g_tx_buf` yeterlidir ve `g_tx_event` ile TC callback doğru kaydı kapatabilir (FR-46).

---

## 6. UART protokolü

### 6.1 Ölçüm mesajları — tam 64 bayt (FR-50, FR-51)

ASCII içerik, boşlukla 63 bayta dolgu, 64. bayt `\n`:

```
TEL,<seq>,<scen>,<uptime_ms>                               ␣␣␣…␣\n   → 64 bayt
BTN,<event_id>,<scen>,PRESSED                              ␣␣␣…␣\n   → 64 bayt
```

İçerik 63 baytı aşarsa **sessizce kesilmez**; `fmt_err` sayacı artırılır ve mesaj hata olarak işaretlenir (FR-52).

### 6.2 Kontrol mesajları — değişken uzunluk, LF sonlu

Bunlar **ölçüm sırasında gönderilmez** (FR-91), dolayısıyla 64 bayt kuralına tabi değildir:

```
REC,<scen>,<id>,<t0>,<t1>,<t2>,<t3>,<t4>,<status>\n     ← kayıt dökümü
CNT,<accepted>,<debounce_rej>,<btnq_drop>,<txq_drop_tel>,
    <txq_drop_btn>,<uart_err>,<rec_ovf>,<txq_hwm>\n     ← sayaç özeti (FR-64)
STA,<state>,<scen>,<count>/<target>\n                   ← durum bildirimi (FR-90)
ACK,<komut>\n  |  ERR,<sebep>\n                         ← komut yanıtı (FR-83)
```

Alınamamış zaman damgası **boş** bırakılır: `REC,S3,18,1500000,1502100,1502400,,,tx_drop` (MR-21).

### 6.3 PC → MCU komutları (FR-84)

| Komut | Etki | Kabul koşulu |
|---|---|---|
| `CMD,SCEN,<S0…S5>\n` | Senaryo parametrelerini uygular, sayaçları sıfırlar | Yalnızca `IDLE` durumunda (FR-85) |
| `CMD,START\n` | Isınmayı başlatır | Yalnızca `IDLE`, senaryo seçiliyken |
| `CMD,STOP\n` | Deneyi iptal eder, `IDLE`'a döner | Her durumda |
| `CMD,DUMP\n` | Kayıtları `REC` satırları olarak döker | Yalnızca `DONE` durumunda |
| `CMD,STAT\n` | `CNT` + `STA` yanıtı üretir | Yalnızca `IDLE` / `DONE` |

Tanınmayan komut → `ERR,unknown\n`; yanlış durumda gelen komut → `ERR,state\n`.

---

## 7. Deney durum makinesi

`ButtonTask` içindeki `experiment_tick()` tarafından yürütülür. **Ayrı görev yoktur** (FR-81).

```
        CMD,SCEN,Sx              CMD,START
  ┌──────────────────┐      ┌──────────────────┐
  │                  ▼      │                  ▼
┌─┴──────┐      ┌────────┐  │  ┌──────────┐  ┌─────────────┐
│  IDLE  │─────►│ ARMED  │──┴─►│ WARMUP   │─►│ MEASURING   │
│        │      │        │     │ 5 saniye │  │ 30 olay     │
└────────┘      └────────┘     └──────────┘  └──────┬──────┘
    ▲                                               │ hedef doldu
    │                                        ┌──────▼──────┐
    │                                        │  DRAINING   │  telemetri dur,
    │                                        │             │  txQ boşalt,
    │                                        └──────┬──────┘  kayıtları kapat
    │                                               │
    │          CMD,DUMP                      ┌──────▼──────┐
    └────────────────────────────────────────│    DONE     │
                    döküm bitti              └─────────────┘

LED:  IDLE=yeşil   WARMUP=turuncu   MEASURING=mavi   DRAINING/DONE=kırmızı
```

| Durum | Davranış |
|---|---|
| `IDLE` | Telemetri kapalı. Buton olayları **kayda alınmaz**. Komut kabul edilir. |
| `ARMED` | Senaryo parametreleri yüklendi, sayaçlar ve kayıt havuzu sıfırlandı (FR-86). |
| `WARMUP` | Telemetri çalışıyor, 5 s sayım. Buton olayları ISR'de reddedilir (FR-87). |
| `MEASURING` | Buton olayları kaydediliyor. Hedef ≥30 kabul edilen basış (MR-01). |
| `DRAINING` | Telemetri durdurulur; `txQ` boşalana kadar beklenir; 1 s içinde kapanmayan kayıtlar `ST_TIMEOUT` yapılır (FR-89, MR-06). |
| `DONE` | Kayıtlar dökülmeye hazır. `CMD,DUMP` beklenir. |

**Senaryo değişimi neden `IDLE` şartına bağlı?** MR-07 karşılaştırılan senaryolar arasında sabitlerin değişmemesini istiyor. Koşan bir deneyin ortasında frekans değişirse, o senaryonun verisi iki farklı koşulun karışımı olur ve karşılaştırma geçersizleşir.

---

## 8. FreeRTOSConfig.h ayarları

| Ayar | Değer | Gerekçe |
|---|---|---|
| `configUSE_PREEMPTION` | 1 | FR-03 |
| `configMAX_PRIORITIES` | **56** | CMSIS-RTOS v2'de CubeMX bu değeri kilitler. FR-04'ü (en az 4) karşılar. Görevler native `xTaskCreate` ile **1 / 2 / 3** önceliğinde açılır; 4…55 kullanılmaz. (bkz. §13) |
| `configTICK_RATE_HZ` | 1000 | 1 ms tick → 10/20/100 ms periyotlar tam sayı tick'e oturur |
| `configUSE_TIMERS` | **0** | Software timer daemon **fazladan bir görev** yaratır → FR-01 riski. CubeMX 1'e zorladığı için USER CODE'da geçersiz kılınır (§13) |
| `configUSE_IDLE_HOOK` | 0 | Idle CPU ölçümü gerekirse 1 yapılır |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | Sessiz bozulma yerine erken yakalama |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | Kuyruk/görev oluşturma hatasını görünür kılar |
| `INCLUDE_uxTaskGetStackHighWaterMark` | 1 | FR-65 |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | 5 | §2.2 öncelik planının dayanağı |
| Heap | `heap_4` | Birleştirme yapar; statik tahsis sonrası fragmantasyon sorunu olmaz |

**Periyot doğrulaması (FR-21):** 1000 Hz tick'te 100 Hz → 10 tick, 50 Hz → 20 tick, 10 Hz → 100 tick. Hepsi tam bölünür, yuvarlama hatası yok. Buna rağmen **gerçek üretim periyodu ölçülüp raporlanacaktır** (AR-11) — planlanan periyot ile gerçekleşen periyot aynı şey değildir.

---

## 9. Hata sayaçları (FR-64)

| Sayaç | Nerede artar |
|---|---|
| `accepted` | EXTI0, filtreden geçen ve kuyruğa yazılan olay |
| `debounce_rej` | EXTI0, 30 ms içindeki tekrar kenar |
| `btnq_drop` | EXTI0, `buttonQ` dolu |
| `txq_drop_tel` | TelemetryTask, `txQ` dolu |
| `txq_drop_btn` | ButtonTask, `txQ` dolu |
| `uart_err` | UartTxTask, `HAL_UART_Transmit_DMA` başarısız |
| `uart_timeout` | UartTxTask, 1 s içinde TC gelmedi |
| `rec_ovf` | Kayıt havuzu slot çakışması |
| `fmt_err` | 63 baytı aşan mesaj içeriği |
| `cmd_overflow` / `cmd_err` | RX komut tamponu taştı / tanınmayan komut |
| `txq_hwm` | `txQ` yüksek su seviyesi (her `xQueueSend` sonrası güncellenir) |

Sayaçlar `CNT` mesajıyla dökülür ve `summary.csv`'ye yazılır (AR-04, AR-11).

---

## 10. PC arayüzü mimarisi

```
   ┌──────────────────┐   satır    ┌───────────┐   olay   ┌──────────────┐
   │ SerialReader     │───────────►│ queue.    │─────────►│ GUI (tkinter)│
   │ thread           │            │ Queue     │  after() │ · durum      │
   │ · pyserial read  │            └───────────┘  100 ms  │ · log        │
   │ · LF tamponlama  │                                   │ · sayaçlar   │
   └──────────────────┘                                   └──────┬───────┘
                                                                 │
            ┌────────────────┬───────────────────┬───────────────┘
            ▼                ▼                   ▼
      ┌───────────┐   ┌─────────────┐    ┌──────────────┐
      │ Parser    │   │ CsvWriter   │    │ Plotter      │
      │ TEL/BTN/  │   │ Sx.csv      │    │ matplotlib   │
      │ REC/CNT/  │   │ summary.csv │    │ 2 grafik     │
      │ STA/ACK   │   └─────────────┘    └──────────────┘
      └───────────┘
```

| Karar | Gerekçe |
|---|---|
| Seri okuma **ayrı thread**'te | tkinter tek thread'lidir; bloklayıcı `read()` arayüzü dondurur |
| Thread → GUI aktarımı **`queue.Queue`** ile | Widget'lara yalnızca ana thread dokunmalıdır |
| GUI `after(100, …)` ile kuyruğu boşaltır | Yoğun akışta (100 Hz) her mesaj için yeniden çizim yapılmaz |
| Çerçeveleme **LF'e göre**, uzunluğa göre değil | UI-02: seri okuma parçalı gelir; LF tek güvenilir sınırdır |
| CSV yazımı `REC` dökümü **tamamlandığında** | UI-08; eksik kayıt varsa önce uyarı (UI-14) |
| Grafikler **CSV'den** üretilir, bellekten değil | AR-08/MR-08: grafik üreten kod ham veriden bağımsız çalışabilmeli |

**Bağımlılıklar:** `pyserial`, `matplotlib`, `pandas`. GUI için `tkinter` (Python ile gelir, ek kurulum yok).

### 10.1 Grafik modülü (UI-20…28)

```
interface/
├── main.py            ← uygulama girişi
├── serial_reader.py   ← okuma thread'i + LF çerçeveleme
├── protocol.py        ← TEL/BTN/REC/CNT/STA/ACK ayrıştırma, CMD üretme
├── recorder.py        ← REC birikimi → Sx.csv, summary.csv
├── plots.py           ← ✦ ÇİZİM MANTIĞI — tek kaynak
├── gui.py             ← tkinter arayüz, plots.py'yi gömer
└── make_plots.py      ← bağımsız betik, plots.py'yi çağırır
```

`plots.py` **tek giriş noktası** sunar ve bir matplotlib `Figure` döndürür — dosya yazmaz, pencere açmaz:

```python
def plot_response_times(csv_paths: list[Path]) -> Figure:
    """Olay numarası → R, 20 ms deadline çizgisiyle (AR-06a)."""

def plot_stage_breakdown(csv_paths: list[Path]) -> Figure:
    """Senaryo → aşama ortalamaları, yığılmış sütun (AR-06b)."""
```

| Tüketici | Nasıl kullanır |
|---|---|
| `gui.py` | `FigureCanvasTkAgg(fig, master=frame)` ile pencereye **gömer** (UI-23) |
| `make_plots.py` | `fig.savefig("analysis/plots/…png")` ile PNG üretir (UI-24) |

**Neden tek modül?** AR-08 grafik üreten kodun depoda olmasını, MR-08 ise tüm grafiklerin **aynı ham veriden** üretilmesini istiyor. Çizim mantığı GUI'nin içine gömülü olsaydı, eğitmen grafikleri bağımsız olarak yeniden üretemezdi. Ayrı olsaydı, GUI ve rapor grafikleri zamanla birbirinden ayrışabilirdi. Tek modül + iki tüketici her ikisini de çözer.

**Girdi daima CSV'dir (UI-26):** GUI canlı akışta `BTN` mesajlarını görse bile grafik çizmez; grafik ancak `REC` dökümü CSV'ye yazıldıktan sonra o dosyadan üretilir. Böylece ekranda gördüğün grafik ile teslim ettiğin PNG **aynı veriden** gelir.

**Filtreleme (UI-27):** `status != "ok"` satırları çizimden dışlanır, ancak dışlanan sayı grafik altyazısında gösterilir — `n=28 (2 dışlandı: 1 tx_drop, 1 timeout)`. Ödev kayıpların gizlenmesini açıkça yasaklıyor (MR-09).

---

## 10.2 Doxygen yapılandırması (DOC-01…12)

| Ayar | Değer | Gerekçe |
|---|---|---|
| `INPUT` | `firmware/Core/Src`, `firmware/Core/Inc` | Üretilmiş HAL/CMSIS sürücüleri hariç |
| `EXCLUDE_PATTERNS` | `*/Drivers/*`, `*/Middlewares/*` | Kendi kodumuzu belgeliyoruz, ST'ninkini değil |
| `OPTIMIZE_OUTPUT_FOR_C` | `YES` | C projesi; C++ varsayımlarını kapatır |
| `EXTRACT_STATIC` | `YES` | Kayıt defteri fonksiyonları `static` — yine de belgelenmeli |
| `WARN_IF_UNDOCUMENTED` | `YES` | DOC-08 |
| `WARN_NO_PARAMDOC` | `YES` | Eksik `@param` yakalanır |
| `GENERATE_HTML` | `YES` | DOC-10 |
| `GENERATE_LATEX` | `NO` | Gereksiz |
| `HAVE_DOT` | `YES` (Graphviz varsa) | Çağrı grafiği, ISR→görev akışını görselleştirir |
| `OUTPUT_DIRECTORY` | `docs/doxygen` | |

### Grup yapısı (DOC-09)

```c
/** @defgroup timing  Zaman Kaynağı ve Damgalar */
/** @defgroup isr     Kesme Servis Rutinleri   */
/** @defgroup tasks   RTOS Görevleri           */
/** @defgroup record  Ölçüm Kayıt Defteri      */
/** @defgroup proto   UART Protokolü           */
/** @defgroup expfsm  Deney Durum Makinesi     */
```

### Yorum şablonu

```c
/**
 * @brief   Buton kenarını yakalar, t₀'ı damgalar ve olayı kuyruğa koyar.
 * @ingroup isr
 *
 * @note    **ISR bağlamında** çalışır. Bloklayıcı çağrı, UART yazımı veya
 *          `printf` içermez (FR-16).
 * @note    NVIC önceliği 5 olmalıdır; `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`
 *          kuralı gereği `...FromISR()` çağırabilmesi buna bağlıdır (FR-17).
 *
 * @par Zaman damgası
 *      **t₀** — fonksiyonun ilk satırında, EXTI bayrağı temizlenmeden önce
 *      okunur (FR-11).
 *
 * @par Paylaşılan durum
 *      Yazar: `g_last_accept_us`, `g_event_counter`, `g_ovf_isr`.
 *      Bunlara yalnızca ISR bağlamı yazar (bkz. `02-design.md` §3.1).
 */
void EXTI0_IRQHandler(void);
```

**Neden bu kadar ayrıntı?** DOC-05 ve DOC-06, ödevin en kolay unutulan iki kuralını koda gömüyor: hangi damganın nerede alındığı, ve hangi değişkene hangi bağlamdan dokunulduğu. Bu yorumlar `code-notes.md` yazılırken doğrudan kaynak malzeme olur (DR-04) — aynı şeyi iki kez düşünmek gerekmez.

**Ölçüme karışmama (UI-11):** Arayüz hiçbir zaman PC saatini MCU zaman damgalarıyla birleştirmez. PC'nin bildiği tek şey "bu satır şu an geldi"dir; bu bilgi yalnızca canlı gösterimde kullanılır, CSV'ye yazılmaz.

---

## 11. Doğrulama planı

| Gereksinim grubu | Nasıl doğrulanacak |
|---|---|
| HW-01…05 | Çoklu ölçer ile seviye kontrolü; şema ile pin çakışma kontrolü; LED blink testi |
| FR-01…06 | `uxTaskGetNumberOfTasks()` = 4 (3 + Idle); öncelikler debugger'da okunur |
| FR-10…18 | Butona hızlı çoklu basış → `debounce_rej` artmalı, `accepted` bir artmalı |
| FR-20…26 | S0'da `TelemetryTask` state = Blocked; `calibrated_work` süresi `timer_us` ile ölçülür |
| FR-40…46 | Logic analyzer / osiloskop ile TX pininde son bit ile t₄ arasındaki fark ölçülür (R-2) |
| FR-50…53 | Arayüz her TEL/BTN çerçevesinin 64 bayt olduğunu sayar (UI-04) |
| FR-60…74 | Kasıtlı kuyruk doldurma testi; sayaçların arttığı gözlenir |
| FR-80…92 | Her komut tek tek gönderilir; yanlış durumda `ERR,state` beklenir |
| MR-01…10 | Senaryo koşu prosedürü (§7) her senaryoda aynı sırayla uygulanır |
| AR-01…13 | `report.md` şablonu bu maddelerin her birine bir bölüm ayırır |

**Bağımsız doğrulama (opsiyonel, elde ekipman varsa):** Logic analyzer ile PA0 kenarı ve PA2 üzerindeki son bit arasındaki gerçek süre ölçülüp firmware'in raporladığı `R` ile karşılaştırılabilir. Bu, t₀ ve t₄ tanımlarının getirdiği sapmayı niceliklendirir ve `report.md`'ye güçlü bir "ne henüz bilinmiyor" maddesi yazdırır (AR-09).

---

## 12. Beklenen sonuçlar (hipotez)

Ölçümden **önce** yazılıyor ki, sonuçlar hipotezi doğrulasa da yanlışlasa da dürüst kalsın (AR-12).

**Hat doluluğu:** `64 bayt × 10 bit × f ÷ 115200`

| Senaryo | Telemetri hattı | Ek CPU talebi | Beklenen baskın aşama |
|---|---|---|---|
| S0 | %0 | %0 | `t₄−t₃` ≈ 5.56 ms; R taban değerine yakın |
| S1 | %5.6 | %0 | S0'a çok yakın |
| S2 | %27.8 | %0 | `t₃−t₂` ölçülebilir şekilde artar |
| S3 | %55.5 | %0 | **`t₃−t₂` baskın** — buton mesajı TX kuyruğunda telemetrinin arkasında bekler |
| S4 | %55.5 | ≈%20 | `t₃−t₂` yüksek kalır, **`t₁−t₀` ve `t₂−t₁` artar** |
| S5 | %55.5 | ≈%50 | `t₁−t₀` / `t₂−t₁` belirgin artar; 20 ms ihlalleri muhtemel |

**Ana hipotez:** İki yük türü **iki farklı aşamayı** bozar. Telemetri frekansı UART hattını doldurarak `t₃−t₂`'yi büyütür; ek CPU yükü ise yüksek öncelikli görev `ButtonTask`'ı preempt ettiği için `t₁−t₀` ve `t₂−t₁`'i büyütür.

**Dikkat:** `t₄−t₃` bir buton mesajı için yaklaşık sabit kalmalıdır (≈5.56 ms hat süresi) — çünkü hat süresi baud'a bağlıdır, yüke değil. Eğer ölçümde `t₄−t₃` senaryolarla birlikte artıyorsa, bu ya TC yolunun yanlış kurulduğuna (R-2) ya da DMA'nın beklenmedik şekilde geciktiğine işarettir. Bu, tasarımın **kendi kendini sınayan** noktasıdır.

**CPU % ile UART % toplanmaz** — biri işlemci zamanı, diğeri hat kapasitesi; farklı kaynaklardır.

---

## 13. Uygulama sırasında alınan kararlar

Tasarım kod üretimiyle karşılaştığında ortaya çıkan sapmalar. Her biri `README.md`'ye de taşınacaktır.

### 13.1 Proje formatı: CubeIDE managed build yerine **CMake** (21.09.2026)

| | |
|---|---|
| **Sorun** | CubeMX 6.18.1'in betik modunda (`-q`) STM32CubeIDE proje dosyalarını üreten iç servis hata vermeden zaman aşımına uğradı; `.cproject` hiç oluşmadı. |
| **Karar** | `ProjectManager.TargetToolchain = CMake`. CubeMX CMake projesini IDE servisine ihtiyaç duymadan kendisi üretir. |
| **Araçlar** | STM32CubeIDE 2.2.0'ın **kendi paketlediği** CMake, Ninja ve GNU Tools for STM32 14.3 kullanılır — ek kurulum yok. |
| **Derleme** | `cmake --preset Debug` → `cmake --build --preset Debug` → `build/Debug/firmware.elf` |
| **Etkisi** | Yok. Kaynak kod, HAL, FreeRTOS ve `.ioc` aynı. CMake, derlemeyi komut satırından tekrarlanabilir kıldığı için DR-03 (derleme adımları) açısından ek fayda sağlar. |

### 13.2 `syscalls.c` / `sysmem.c` paketten kopyalandı

CubeMX betik modunda bu iki dosyayı yazarken yolu hatalı birleştiriyor (`firmware\D:\RTOS\…`). Dosyalar ST'nin her projede aynı olan newlib stub'larıdır; `STM32Cube_FW_F4_V1.28.3` paketindeki resmi kopyalarından alındı. Linker betiğinin gerektirdiği `_estack`, `_Min_Stack_Size`, `_end` sembolleri doğrulandı.

### 13.3 CubeMX'in zorladığı FreeRTOS ayarları — USER CODE ile düzeltildi

CMSIS-RTOS v2 seçiliyken CubeMX bazı değerleri `.ioc`'den bağımsız olarak zorluyor. Düzeltmeler **USER CODE blokları** içinde olduğu için yeniden üretimde korunur.

| Ayar | CubeMX'in ürettiği | Düzeltme | Yer |
|---|---|---|---|
| `configUSE_TIMERS` | 1 | `#undef` → **0** | `FreeRTOSConfig.h` USER CODE Defines |
| `INCLUDE_xTimerPendFunctionCall` | 1 | → **0** (timer kapalıyken `timers.c` derlenmez) | aynı |
| `configUSE_OS2_EVENTFLAGS_FROM_ISR` | 1 | → **0** (yukarıdakine bağımlı; kullanılmıyor) | aynı |
| `defaultTask` | üretiliyor (öncelik 24) | İlk koşusunda **kendini siler**: `vTaskDelete(NULL)` | `freertos.c` USER CODE StartDefaultTask |

> ⚠️ **İlk deneme hatalıydı (T-07'de bulundu):** `defaultTask` önce scheduler başlamadan dışarıdan `osThreadTerminate()` ile siliniyordu. FreeRTOS scheduler öncesinde `pxCurrentTCB`'yi en yüksek öncelikli göreve (24) ayarladığı için bu silme "kendini silme" yoluna düştü; görev yalnızca bekleme listesine kondu ve öncelik 1–3'teki görevlerimiz `pxCurrentTCB`'yi değiştirmediği için scheduler **silinmiş görevi başlattı**. Sonuç: her 1 ms'de diğer görevleri kesen bir zombi. `g_diag.task_count = 5` ile yakalandı; düzeltmeden sonra 4.

**Doğrulama (ELF sembol tablosu):** `prvTimerTask` / `xTimerCreateTimerTask` → **0 sembol** (timer daemon yok). `prvIdleTask` → 1 sembol.

### 13.4 Derleme sonucu (T-02 kanıtı)

```
0 hata · 0 uyarı
RAM     25 008 B / 128 KB   (%19)
CCMRAM       0 B /  64 KB   (%0)    ← HW-05: DMA tamponları CCM'e düşmüyor
FLASH   22 092 B /   1 MB   (%2)
```

RAM'in büyük kısmı `configTOTAL_HEAP_SIZE = 20480` (FreeRTOS heap). Görevler ve kuyruklar bu heap'ten ayrılacak.

---

## Onay

- [x] Kullanıcı tasarımı okudu ve onayladı. *(21.09.2026 — "başlayabiliriz")*
- [x] R-2 (TC yolu) doğrulama adımı task listesine eklendi. *(T-08)*
- [x] `03-tasks.md` yazımına geçilebilir.
