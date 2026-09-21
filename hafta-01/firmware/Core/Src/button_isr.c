/**
 * @file    button_isr.c
 * @brief   Kullanıcı butonu kesmesinin gerçekleştirimi (T-06).
 * @ingroup isr
 *
 * Akış (tek bir kesme çağrısı içinde):
 * 1. `EXTI0_IRQHandler` → @ref button_irq_entry : t₀ okunur.
 * 2. HAL EXTI bayrağını temizler → `HAL_GPIO_EXTI_Callback()` çağrılır.
 * 3. Geri çağrı: 30 ms filtresi → kimlik ata → kayıt aç → `buttonQ`'ya yaz.
 */

#include "button_isr.h"

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_config.h"
#include "app_diag.h"
#include "app_tasks.h"
#include "app_types.h"
#include "record.h"
#include "timing.h"

#include <stdbool.h>

#if !HW_SELFTEST   /* öz-test kendi EXTI geri çağrısını tanımlar */

/**
 * @brief   Bu kesme çağrısında yakalanan t₀.
 * @par Paylaşılan durum
 *      Yazar ve okur aynı kesme çağrısıdır (@ref button_irq_entry yazar, geri çağrı
 *      okur). EXTI0 kendini kesemediği için yarış yoktur.
 */
static volatile uint32_t s_entry_t0;

/**
 * @brief   Son **kabul edilen** kenarın zamanı [µs] ve bunun geçerli olup olmadığı.
 * @par Paylaşılan durum
 *      Yalnızca buton ISR'ı okur ve yazar.
 */
static uint32_t s_last_accept_us;
static bool     s_have_accept;      /**< FR-13: ilk olay filtreden muaf olsun diye. */

/** @brief Son atanan olay kimliği; ilk kimlik 1'dir. @par Paylaşılan durum Yalnızca buton ISR'ı. */
static uint32_t s_event_counter;

/**
 * @brief   Filtre silahlı mı: `true` ise sonraki basış kenarı kabul edilebilir (FR-12b).
 *
 * Başlangıçta açıktır; ilk basış filtrelenmez (FR-13).
 *
 * @par Paylaşılan durum
 *      **İki yazar**, bu yüzden kuralı açıkça: ISR yalnızca kapatır (kabul anında),
 *      @ref button_rearm_poll yalnızca açar ve bunu ISR'ı maskeleyen bir kritik
 *      bölüm içinde yapar. Tek baytlık okuma/yazma atomiktir.
 */
static volatile bool s_armed = true;

void button_irq_entry(void)
{
    s_entry_t0 = timer_us();
}

void button_rearm_poll(void)
{
    static bool     s_low_seen;       /* son çağrıdan beri kesintisiz LOW mu */
    static uint32_t s_low_since_us;   /* ilk LOW gözleminin zamanı */

    if (s_armed) {
        s_low_seen = false;
        return;
    }

    const uint32_t now = timer_us();

    if (HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin) != GPIO_PIN_RESET) {
        s_low_seen = false;           /* hâlâ basılı veya zıplıyor: süre baştan */
        return;
    }
    if (!s_low_seen) {
        s_low_seen     = true;
        s_low_since_us = now;
        return;
    }
    if ((uint32_t)(now - s_low_since_us) < APP_REARM_LOW_US) {
        return;
    }

    /* Pin okuma ile bayrak yazma arasına bir basış kenarı girerse: EXTI bekleyen
     * kesme olarak kilitlenir, kritik bölümden çıkınca silah açık bulunur ve
     * kabul edilir. Bölüm yalnızca birkaç komut sürer. */
    taskENTER_CRITICAL();
    if (HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin) == GPIO_PIN_RESET) {
        s_armed = true;
    }
    taskEXIT_CRITICAL();
    s_low_seen = false;
}

/**
 * @brief   HAL'in EXTI geri çağrısı: filtre, kayıt açma ve olayı kuyruğa koyma.
 * @param   GPIO_Pin  Kesmeyi üreten pin.
 *
 * @note    **ISR bağlamı** (öncelik 5). Beklemez, UART'a yazmaz (FR-16).
 * @note    İki aşamalı filtre (tasarım §4.1.1):
 *          1. Son **kabul edilen** kenardan 30 ms içindeki kenar → `debounce_rej`
 *             (basış zıplaması, FR-12). Reddedilen kenar pencereyi uzatmaz.
 *          2. Silah kapalıyken gelen kenar → `unarmed_rej` (bırakış zıplaması veya
 *             basılı tutma, FR-12b). Silahı yalnızca @ref button_rearm_poll açar.
 *          Sıra bilinçlidir: iki olgu raporda ayrı sayılabilsin.
 * @note    Kuyruk henüz oluşturulmamışsa (açılışın ilk mikrosaniyeleri) olay yok sayılır.
 *
 * @par Zaman damgası
 *      **t₀** @ref button_irq_entry tarafından alınmıştır; burada kayda yazılır.
 * @par Paylaşılan durum
 *      Yazar: `g_cnt_isr.accepted / debounce_rej / btnq_drop / rec_ovf` (yalnızca ISR).
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin != BTN_USER_Pin || g_button_q == NULL) {
        return;
    }

    const uint32_t t0 = s_entry_t0;

    if (s_have_accept && (uint32_t)(t0 - s_last_accept_us) < APP_DEBOUNCE_US) {
        g_cnt_isr.debounce_rej++;                        /* FR-12 */
        return;
    }
    if (!s_armed) {
        g_cnt_isr.unarmed_rej++;                         /* FR-12b */
        return;
    }
    s_armed          = false;
    s_have_accept    = true;
    s_last_accept_us = t0;

    const uint32_t id = ++s_event_counter;              /* FR-14 */

    if (!rec_open(id, t0)) {
        g_cnt_isr.rec_ovf++;                             /* FR-61 */
        return;
    }

    const ButtonEvent e = { .event_id = id, .t0_us = t0 };
    BaseType_t woken = pdFALSE;

    if (xQueueSendFromISR(g_button_q, &e, &woken) != pdPASS) {   /* FR-15 */
        g_cnt_isr.btnq_drop++;
        (void)rec_close(id, REC_BTNQ_DROP);              /* FR-18: kimlik ve t₀ korunur */
    } else {
        g_cnt_isr.accepted++;
    }

    /* ButtonTask (öncelik 2) şu an koşan görevden yüksekse, kesmeden çıkar
     * çıkmaz ona geçilir; bir sonraki tick beklenmez. */
    portYIELD_FROM_ISR(woken);
}

#endif /* !HW_SELFTEST */
