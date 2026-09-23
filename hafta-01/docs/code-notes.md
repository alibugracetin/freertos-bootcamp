# Kod notları — kritik bloklar ve gerekçeleri

Bu dosya, ölçümün doğruluğunu belirleyen kod parçalarını açıklar. Her sembolün ayrıntılı dokümantasyonu için: `docs/doxygen/html/index.html`.

---

## 1. t₀ — kesmenin ilk işi

Zaman damgası, HAL'in EXTI bayrağını temizlemesinden **önce** alınır. CubeMX'in ürettiği kesme işleyicisindeki USER CODE bloğu bunun için kullanılır:

```c
/* stm32f4xx_it.c */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_IRQn 0 */
  button_irq_entry();          /* t₀ = TIM2->CNT */
  /* USER CODE END EXTI0_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(BTN_USER_Pin);   /* bayrağı temizler, geri çağrıyı çalıştırır */
}
```

```c
/* button_isr.c */
void button_irq_entry(void) { s_entry_t0 = timer_us(); }
```

**Neden bu kadar erken?** Bayrak temizleme bir periferik yazmasıdır ve tamamlanması birkaç çevrim sürer. Ayrıca HAL'in işleyicisi de zaman alır. Damgayı sonra alsaydık bu yükler ölçüme karışırdı.

**Neden ara değişken yarış oluşturmuyor?** `s_entry_t0` aynı kesme çağrısı içinde yazılıp okunur. EXTI0 kendini kesemez, dolayısıyla araya başka bir yazma giremez.

**Ölçülen kanıt:** Yoklamayla (polling) alınan damgaların hepsi aynı mikrosaniye rakamlarıyla bitiyordu (`…954`), çünkü yoklama döngüsü hep aynı tick fazında uyanıyor ve 0–5 ms nicemleme gürültüsü ekliyordu. Kesmede bu yok.

## 2. İki aşamalı buton filtresi

```c
/* button_isr.c — HAL_GPIO_EXTI_Callback */
if (s_have_accept && (uint32_t)(t0 - s_last_accept_us) < APP_DEBOUNCE_US) {
    g_cnt_isr.debounce_rej++;      /* basış anındaki zıplama (FR-12) */
    return;
}
if (!s_armed) {
    g_cnt_isr.unarmed_rej++;       /* bırakış zıplaması / basılı tutma (FR-12b) */
    return;
}
s_armed = false;
```

Silahı yalnızca görev tarafı açar; buton en az 30 ms bırakılmış görülmelidir:

```c
/* button_isr.c — button_rearm_poll(), ButtonTask bakım turundan çağrılır */
taskENTER_CRITICAL();
if (HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin) == GPIO_PIN_RESET) {
    s_armed = true;
}
taskEXIT_CRITICAL();
```

**Neden kritik bölüm?** Pin okumasıyla bayrak yazması arasına bir basış kenarı girerse, o kenar silah kapalıyken gelmiş sayılıp reddedilirdi. Kritik bölüm EXTI0'ı (öncelik 5) maskeler; bu sırada gelen kenar bekleyen kesme olarak kilitlenir ve çıkışta silah açık bulunarak kabul edilir. Bölüm birkaç komut sürer ve ölçüm yolunda değildir.

**Neden gerekliydi?** İlk sürümde yalnızca 30 ms filtresi vardı. Ölçüm: 5 fiziksel basış → **8 kabul edilen olay**. Kayıt defterindeki t₀ zaman çizelgesi fazladan olayların önceki basıştan 176 / 1 318 / 1 240 ms sonra geldiğini gösterdi — bunlar bırakış zıplamalarıydı. Resmî ölçümde `unarmed_rej` senaryo başına 34–72 arasında; bu kenarlar basış sayılsaydı 30 yerine ~90 olay kaydedilirdi.

## 3. Kayıt defteri — beş bağlamın doldurduğu form

Bir olayın damgaları beş ayrı yerde alınır (t₀ buton ISR, t₁/t₂ ButtonTask, t₃ UartTxTask, t₄ TC ISR). Kayıt bunları olay kimliğiyle birleştirir:

```c
/* record.c */
bool rec_stamp(uint32_t id, TsIndex idx, uint32_t t_us)
{
    volatile EventRecord *r = slot_of(id);          /* id % 64 */
    if (idx >= TS_COUNT || r->event_id != id || r->status != REC_OPEN) {
        return false;                                /* slot başkasına ait: yazma */
    }
    r->t[idx] = t_us;
    r->have  |= (uint8_t)(1u << idx);
    return true;
}
```

**Kilit neden yok?** Aynı kaydın alanları kesin bir sırayla yazılır: t₁ ancak olay kuyruktan alınınca, t₃ ancak mesaj TX kuyruğundan çekilince, t₄ ancak DMA başlatılınca oluşabilir. İki bağlam aynı kayda aynı anda yazmaz. Farklı olaylar farklı slotlara düşer. Bu, tasarımda açıkça gerekçelendirilmiştir; kesme kapatmak t₀ ve t₄'ün doğruluğunu bozardı.

**`have` bit maskesi neden var?** Alınamamış damga CSV'de **boş** kalmalıdır. `t == 0` ile "alınamadı" ayırt edilemez, çünkü sayaç gerçekten 0'dan geçer.

**Açılışta öz-test:** `rec_selfcheck()` 100 sahte olay açar; 64'ü kabul edilir, 36'sı taşma olarak reddedilir. Slot sahipliği, iki kez kapatma ve sıfırlama kuralları da sınanır. Sonuç `g_diag.selfcheck` alanında.

## 4. t₄ — "DMA bitti" ile "son bit çıktı" aynı değil

Ödevin uyardığı tuzak. HAL 1.8.5 kaynağı okunarak doğrulandı:

```c
/* stm32f4xx_hal_uart.c — DMA tamamlandığında */
static void UART_DMATransmitCplt(DMA_HandleTypeDef *hdma)
{
    ...
    ATOMIC_SET_BIT(huart->Instance->CR1, USART_CR1_TCIE);   /* geri çağrı YOK, TC kesmesi açılır */
}

/* USART TC bayrağı kalktığında */
static HAL_StatusTypeDef UART_EndTransmit_IT(UART_HandleTypeDef *huart)
{
    __HAL_UART_DISABLE_IT(huart, UART_IT_TC);
    HAL_UART_TxCpltCallback(huart);                          /* t₄ buradan gelir */
}
```

Yani `TxCpltCallback` gerçekten son stop biti hattan çıktıktan sonra çağrılır. Bu yalnızca **normal mod** DMA için geçerlidir; circular modda geri çağrı DMA tamamlanmasında gelir. Projede normal mod kullanılıyor.

Çalışma zamanında da ölçüldü: DMA kesmesi ile TC kesmesi arasındaki fark **173 µs**, yani tam iki karakter süresi (veri register'ı + kaydırma register'ı). t₄'ü DMA kesmesinde alsaydık her ölçüm 173 µs kısa çıkardı.

```c
/* uart_link.c */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    const uint32_t t4 = s_uart_entry_us;         /* USART2 ISR girişinde yakalandı */
    if (s_tx_kind == MSG_BTN) {
        rec_stamp(id, TS_T4, t4); rec_close(id, REC_OK);
        g_diag.last_dma_to_tc_us = t4 - s_dma_entry_us;   /* R-2 kanıtı */
    }
    vTaskNotifyGiveFromISR(s_tx_task, &woken);
}
```

## 5. Gönderim tamponu DMA sürerken yaşamalı

```c
/* uart_link.c */
static uint8_t s_tx_buf[APP_TX_BUF_LEN];     /* kalıcı, .bss → normal SRAM */

memcpy(s_tx_buf, m.data, m.len);             /* m görev stack'inde, DMA ona bakamaz */
const uint32_t t3 = timer_us();
rec_stamp(m.event_id, TS_T3, t3);            /* t₃ başlatmadan ÖNCE yazılır */
HAL_UART_Transmit_DMA(&huart2, s_tx_buf, m.len);
ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));   /* TC bekle; sonsuz bekleme yok */
```

İki nokta: (1) DMA'nın okuduğu bellek aktarım bitene kadar geçerli olmalıdır, görev yereli değil. (2) `s_tx_buf` normal SRAM'dedir; F407'nin CCM RAM'ine (`0x10000000`) DMA erişemez. Linker raporu `CCMRAM: 0 B` diyerek bunu doğrular.

**t₃ neden başlatmadan önce yazılıyor?** TC kesmesi kaydı `REC_OK` ile kapatır. t₃ sonra yazılsaydı, hızlı bir TC'de kayıt t₃ yazılmadan kapanabilirdi.

## 6. Zaman kaynağı ve sarma

```c
/* timing.h */
static inline uint32_t timer_us(void) { return TIM2->CNT; }
```

HAL sarmalayıcısı yok: tek bir hizalı 32-bit register okuması Cortex-M4'te atomiktir, ISR'dan güvenlidir ve çağrı yükü eklemez.

```c
uint32_t delta = t_end - t_start;      /* mod 2³², sarma otomatik doğru */
```

TIM2 1 MHz'de 2³² µs'de (≈71,6 dakika) sarar. Bir olayın toplam süresi bundan çok kısa olduğu için işaretsiz çıkarma her durumda doğru sonucu verir. Doğrulandı: `0x00000010 − 0xFFFFFFF0 = 32`.

⚠️ `MX_TIM2_Init()` timer'ı yapılandırır ama **başlatmaz**. `HAL_TIM_Base_Start(&htim2)` çağrılmazsa tüm damgalar 0 çıkar.

## 7. Ek CPU yükü — optimize edilemeyen hesap

```c
/* experiment.c */
static volatile uint32_t s_work_sink;

void calibrated_work(uint32_t iters)
{
    uint32_t acc = 0x12345678u;
    for (uint32_t i = 0; i < iters; i++) {
        acc = acc * 1664525u + 1013904223u;    /* her adım öncekine bağlı */
    }
    s_work_sink = acc;                          /* volatile: sonuç kullanılıyor */
}
```

Her iterasyon bir öncekinin sonucuna bağlı olduğu için derleyici döngüyü açamaz veya paralelleştiremez; `volatile` yazma nedeniyle tamamen silemez. `vTaskDelay` kullanılmaz — o CPU'yu **bırakır**, yük üretmez.

**Kalibrasyon.** Açılışta ölçülen iterasyon süresi 143,1 ns, koşan deneyde 125,4 ns. Fark flash önbelleğindendir (ilk çalıştırma soğuk). Sabitler deney koşullarındaki değerle hesaplandı; ölçülen sonuç S4 = 2 001,6 µs, S5 = 5 008,4 µs.

**Sabitin karta gittiğini doğrulama.** Başlık dosyası değiştiğinde derleme aracı bunu atlayabilir (Windows'ta dosya zaman damgası çözünürlüğü 1 saniye). Bir kez bu yaşandı: derleme "0 hata" dedi, ama flash'ta eski değer duruyordu. Doğrulama:

```bash
ADDR=$(arm-none-eabi-nm -S build/Debug/firmware.elf | grep ' k_scen$' | awk '{print $1}')
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 "0x$ADDR" 72
```

## 8. Bakım turu ölçüm yolundan çıkarıldı

```c
/* app_tasks.c — ButtonTask */
const BaseType_t got = xQueueReceive(g_button_q, &e, pdMS_TO_TICKS(APP_BUTTON_POLL_MS));
if (got == pdPASS) {
    /* t₁ → yanıt → t₂ → txQ  (hiçbir ek iş yok) */
}
const TickType_t now = xTaskGetTickCount();
if (got != pdPASS || (now - last_maint) >= pdMS_TO_TICKS(APP_BUTTON_POLL_MS)) {
    last_maint = now;
    button_rearm_poll();
    exp_tick();
    g_diag.btn_stack_hwm = uxTaskGetStackHighWaterMark(NULL);   /* stack taraması */
    ...
}
```

**Neden?** `UartTxTask` (öncelik 1), `ButtonTask` (öncelik 2) bloklanana kadar koşamaz. Gönderimden sonra bu görevde yapılan her iş doğrudan `t₃ − t₂`'ye eklenir. İlk uçtan uca ölçümde boş kuyrukta bile `t₃ − t₂ = 125 µs` çıktı; büyük kısmı stack'i tarayan tanı çağrısıydı. Bakım boşta çalışacak şekilde taşındıktan sonra resmî ölçümde S0'da `t₃ − t₂ = 34 µs`.

Bu, ölçüm aletinin ölçtüğü şeyi değiştirmesinin somut bir örneğidir.

## 9. CubeMX'in zorladıkları

CMSIS-RTOS v2 seçiliyken CubeMX bazı ayarları `.ioc`'den bağımsız olarak dayatır. Düzeltmeler USER CODE bloklarındadır, yeniden üretimde korunur.

```c
/* FreeRTOSConfig.h — USER CODE BEGIN Defines */
#undef  configUSE_TIMERS
#define configUSE_TIMERS 0                 /* timer daemon = gizli 4. görev */
#undef  INCLUDE_xTimerPendFunctionCall
#define INCLUDE_xTimerPendFunctionCall 0
#undef  configUSE_OS2_EVENTFLAGS_FROM_ISR
#define configUSE_OS2_EVENTFLAGS_FROM_ISR 0
```

```c
/* freertos.c — USER CODE BEGIN StartDefaultTask */
vTaskDelete(NULL);      /* CubeMX'in defaultTask'ı ilk koşusunda kendini siler */
```

**defaultTask neden scheduler öncesi silinemez?** Denendi ve hataya yol açtı. FreeRTOS scheduler başlamadan `pxCurrentTCB`'yi en yüksek öncelikli göreve ayarlar; defaultTask öncelik 24'tedir. Dışarıdan `vTaskDelete` çağrısı bunu "kendini silen görev" sanıp yalnızca bekleme listesine koyar. Bizim görevlerimiz (öncelik 1–3) `pxCurrentTCB`'yi değiştirmediği için scheduler **silinmiş görevi başlatır**: `osDelay(1)` ile her 1 ms'de diğer görevleri kesen bir zombi. `g_diag.task_count` 4 yerine 5 göstererek yakalandı.

Doğrulama: ELF sembol tablosunda `prvTimerTask` ve `xTimerCreateTimerTask` **yok**; çalışırken `task_count = 4` (3 uygulama + Idle).

## 10. Sayaçlarda tek yazar kuralı

```c
/* app_diag.h */
extern volatile IsrCounters  g_cnt_isr;    /* yalnızca ISR'lar yazar */
extern volatile TaskCounters g_cnt_task;   /* her alanın tek yazar görevi var */
```

`sayac++` atomik değildir. İki bağlam aynı sayacı artırırsa sayım kaybolur. Çözüm kritik bölüm değil, **ayrıştırma**: ISR'ların saydıkları ile görevlerin saydıkları ayrı yapılardadır ve her alanın tek bir yazarı vardır. Böylece ölçüm yolunda hiç kesme kapatılmaz.

İki yazarı olan tek değişken buton filtresinin silah bayrağıdır; orada kural açıktır: ISR yalnızca kapatır, görev yalnızca açar ve bunu kritik bölüm içinde yapar (§2).

## 11. Ölçümü bozmayan tanı

`g_diag` yapısı ST-LINK üzerinden, işlemci durdurulmadan okunabilir:

```bash
python hafta-01/tools/read_diag.py
```

UART'a hiç trafik eklemez, bu yüzden ölçüm sırasında da kullanılabilir. Görev sayısı, heap, stack yüksek su seviyesi, son olayın gecikmesi ve DMA→TC farkı buradan okunur. Zombi görev hatası ve kalibrasyon sapması bu araçla yakalandı.
