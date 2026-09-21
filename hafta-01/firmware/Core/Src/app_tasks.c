/**
 * @file    app_tasks.c
 * @brief   Görevlerin, kuyrukların ve sayaçların gerçekleştirimi.
 * @ingroup tasks
 *
 * **Durum (T-07):** Görevler iskelet halindedir. Kuyruk bağlantıları ve bloklama
 * noktaları son haliyle kuruludur; mesaj üretimi (T-09), UART gönderimi (T-08)
 * ve deney durum makinesi (T-13) sonraki adımlarda eklenecektir.
 */

#include "app_tasks.h"

#include "main.h"
#include "task.h"

#include "app_config.h"
#include "app_diag.h"
#include "app_types.h"
#include "button_isr.h"
#include "record.h"
#include "timing.h"

QueueHandle_t g_button_q = NULL;
QueueHandle_t g_tx_q     = NULL;

volatile IsrCounters  g_cnt_isr;
volatile TaskCounters g_cnt_task;
volatile AppDiag      g_diag;

static TaskHandle_t s_telemetry_task;
static TaskHandle_t s_button_task;
static TaskHandle_t s_uarttx_task;

/**
 * @brief   Telemetri görevi — **yüksek öncelik (3)**.
 * @param   arg  Kullanılmıyor.
 *
 * **T-07 iskeleti = S0 davranışı:** Telemetri kapalıyken görev bir bildirim
 * bekleyerek **süresiz bloklanır**; ready listesinde bulunmaz ve CPU tüketmez
 * (FR-22). Periyodik üretim (`xTaskDelayUntil`) T-13'te senaryolarla eklenecek.
 */
static void TelemetryTask(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

/**
 * @brief   Buton görevi — **orta öncelik (2)**.
 * @param   arg  Kullanılmıyor.
 *
 * `buttonQ`'da en fazla @ref APP_BUTTON_POLL_MS bloklanır; ISR bir olay koyduğunda
 * hemen uyanır ve t₁'i kaydeder. Olay gelmezse bakım turunda buton filtresini
 * yeniden silahlandırır (FR-12b); deney durum makinesi de T-13'te bu tura eklenecek.
 * **T-07 iskeleti:** Yanıt mesajı (t₂) henüz üretilmiyor; bunun yerine yeşil LED
 * her olayda durum değiştirir ve t₁ − t₀ tanı yapısına yazılır.
 *
 * @par Zaman damgası
 *      **t₁** — `xQueueReceive` döner dönmez, başka hiçbir işten önce (FR-31).
 *      Bu nedenle t₁ − t₀; ISR'ın kalan süresini, bağlam geçişini ve
 *      ButtonTask'ın CPU'yu bekleme süresini içerir.
 * @par Paylaşılan durum
 *      Yazar: `g_diag` (btn_* ve last_* alanları), `g_cnt_task.rec_mismatch`.
 */
static void ButtonTask(void *arg)
{
    (void)arg;

    for (;;) {
        ButtonEvent e;

        /* Olay gelirse hemen döner (t₁ gecikmesi etkilenmez). Gelmezse en geç
         * APP_BUTTON_POLL_MS sonra döner ve bakım turu çalışır (FR-30). */
        if (xQueueReceive(g_button_q, &e, pdMS_TO_TICKS(APP_BUTTON_POLL_MS)) != pdPASS) {
            button_rearm_poll();                                 /* FR-12b */
            continue;
        }
        const uint32_t t1 = timer_us();                          /* t₁ — FR-31 */

        if (!rec_stamp(e.event_id, TS_T1, t1)) {
            g_cnt_task.rec_mismatch++;
        }

        const uint32_t lat = t1 - e.t0_us;                       /* mod 2³² */
        g_diag.btn_wakeups++;
        g_diag.last_event_id    = e.event_id;
        g_diag.last_t0          = e.t0_us;
        g_diag.last_t1          = t1;
        g_diag.last_t1_minus_t0 = lat;
        if (lat > g_diag.max_t1_minus_t0) {
            g_diag.max_t1_minus_t0 = lat;
        }
        g_diag.btn_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        /* Her olayda güncellenir: silinen defaultTask'ın belleğini Idle görevi
         * ancak tüm görevler bloklandığında geri verir; ilk anda okumak 5 gösterirdi. */
        g_diag.task_count    = (uint32_t)uxTaskGetNumberOfTasks();
        g_diag.heap_free     = (uint32_t)xPortGetFreeHeapSize();
        g_diag.heap_min_free = (uint32_t)xPortGetMinimumEverFreeHeapSize();

        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    }
}

/**
 * @brief   UART gönderim görevi — **düşük öncelik (1)**. UART'ın tek sahibi (FR-05).
 * @param   arg  Kullanılmıyor.
 *
 * **T-07 iskeleti:** `txQ`'da bloklanır. Henüz kimse kuyruğa yazmadığı için
 * hiç uyanmaz. DMA gönderimi, t₃ ve TC bekleme T-08'de eklenecek.
 */
static void UartTxTask(void *arg)
{
    (void)arg;
    for (;;) {
        TxMsg m;
        (void)xQueueReceive(g_tx_q, &m, portMAX_DELAY);
    }
}

void app_init_early(void)
{
    uint32_t opened = 0u, rejected = 0u;

    timing_init();

    g_diag.magic              = APP_DIAG_MAGIC;
    g_diag.selfcheck          = rec_selfcheck(&opened, &rejected) ? 1u : 2u;
    g_diag.selfcheck_opened   = opened;
    g_diag.selfcheck_rejected = rejected;
}

void app_create_rtos_objects(void)
{
    g_button_q = xQueueCreate(APP_BUTTON_QUEUE_LEN, sizeof(ButtonEvent));
    g_tx_q     = xQueueCreate(APP_TX_QUEUE_LEN, sizeof(TxMsg));
    configASSERT(g_button_q != NULL && g_tx_q != NULL);

    /* Hata ayıklayıcının kuyruk görünümünde isimleriyle görünsünler. */
    vQueueAddToRegistry(g_button_q, "buttonQ");
    vQueueAddToRegistry(g_tx_q, "txQ");

    /* Her dönüş değeri pdPASS ile ayrı ayrı karşılaştırılır. Başarısızlık -1 döner;
     * `ok &= xTaskCreate(...)` gibi bit düzeyinde birleştirme 1 & -1 = 1 verip
     * hatayı gizlerdi. */
    const BaseType_t r_tel = xTaskCreate(TelemetryTask, "Telemetry", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_TELEMETRY, &s_telemetry_task);
    const BaseType_t r_btn = xTaskCreate(ButtonTask, "Button", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_BUTTON, &s_button_task);
    const BaseType_t r_tx  = xTaskCreate(UartTxTask, "UartTx", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_UARTTX, &s_uarttx_task);
    configASSERT(r_tel == pdPASS && r_btn == pdPASS && r_tx == pdPASS);
}
