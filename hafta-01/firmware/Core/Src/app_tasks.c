/**
 * @file    app_tasks.c
 * @brief   Kuyruklar, görev oluşturma, açılış sırası ve ButtonTask.
 * @ingroup tasks
 *
 * Görev gövdeleri sorumluluklarına göre dağıtılmıştır:
 * - `ButtonTask` — bu dosya (olay → yanıt, t₁ / t₂; bakım turu)
 * - `TelemetryTask` — `experiment.c` (yük üreteci, senaryoya bağlı)
 * - `UartTxTask` — `uart_link.c` (UART'ın tek sahibi, t₃ / t₄)
 */

#include "app_tasks.h"

#include "main.h"
#include "task.h"

#include "app_config.h"
#include "app_diag.h"
#include "app_types.h"
#include "button_isr.h"
#include "experiment.h"
#include "protocol.h"
#include "record.h"
#include "timing.h"
#include "uart_link.h"

QueueHandle_t g_button_q = NULL;
QueueHandle_t g_tx_q     = NULL;

volatile IsrCounters  g_cnt_isr;
volatile TaskCounters g_cnt_task;
volatile AppDiag      g_diag;

/**
 * @brief   Buton görevi — **orta öncelik (2)**.
 * @param   arg  Kullanılmıyor.
 *
 * `buttonQ`'da en fazla @ref APP_BUTTON_POLL_MS bloklanır. Olay gelince:
 * t₁ → 64 baytlık `BTN` yanıtı → t₂ → `txQ`'ya bloklamadan gönder (FR-30…35).
 * Olay yokken (en geç 50 ms'de bir) bakım turu: filtre silahı (FR-12b), deney
 * durum makinesi ve tanı alanları. Bakım olaydan hemen sonra yapılmaz; aksi halde
 * süresi t₃ − t₂'ye eklenirdi.
 *
 * @par Zaman damgası
 *      **t₁** — `xQueueReceive` döner dönmez (FR-31).
 *      **t₂** — `xQueueSend` çağrısından hemen önce (FR-32). Kayda gönderimden
 *      *sonra* yazılır; bu güvenlidir çünkü mesajı alacak `UartTxTask` (öncelik 1)
 *      bu görev bloklanana kadar koşamaz, yani t₃ ve t₄ t₂'den önce yazılamaz.
 *      Böylece kayıt yazma süresi t₂ − t₁'e girer, t₃ − t₂'ye girmez.
 * @par Paylaşılan durum
 *      Yazar: `g_cnt_task.rec_mismatch / txq_drop_btn / fmt_err_btn / cmd_err`,
 *      `g_diag` btn_* / last_* / task_count / heap_* alanları.
 */
static void ButtonTask(void *arg)
{
    (void)arg;
    TickType_t last_maint = xTaskGetTickCount();

    for (;;) {
        ButtonEvent e;
        const BaseType_t got = xQueueReceive(g_button_q, &e, pdMS_TO_TICKS(APP_BUTTON_POLL_MS));

        if (got == pdPASS) {
            const uint32_t t1 = timer_us();                      /* t₁ — FR-31 */
            if (!rec_stamp(e.event_id, TS_T1, t1)) {
                g_cnt_task.rec_mismatch++;
            }

            TxMsg m;
            if (!proto_fmt_fixed(&m, MSG_BTN, e.event_id, "BTN,%lu,%s,PRESSED",
                                 (unsigned long)e.event_id, exp_scenario_name())) {
                g_cnt_task.fmt_err_btn++;                        /* FR-52 */
                (void)rec_close(e.event_id, REC_TX_ERROR);
            } else {
                const uint32_t t2 = timer_us();                  /* t₂ — FR-32 */
                const BaseType_t sent = xQueueSend(g_tx_q, &m, 0);
                if (!rec_stamp(e.event_id, TS_T2, t2)) {
                    g_cnt_task.rec_mismatch++;
                }
                if (sent != pdPASS) {
                    g_cnt_task.txq_drop_btn++;                   /* FR-34 */
                    (void)rec_close(e.event_id, REC_TXQ_DROP);
                }
            }

            const uint32_t lat = t1 - e.t0_us;
            g_diag.btn_wakeups++;
            g_diag.last_event_id    = e.event_id;
            g_diag.last_t0          = e.t0_us;
            g_diag.last_t1          = t1;
            g_diag.last_t1_minus_t0 = lat;
            if (lat > g_diag.max_t1_minus_t0) {
                g_diag.max_t1_minus_t0 = lat;
            }
        }

        /* Bakım turu olaydan hemen sonra DEĞİL, olay yokken çalışır (en geç 50 ms'de bir).
         *
         * Neden: UartTxTask (öncelik 1) bu görev (öncelik 2) bloklanana kadar koşamaz.
         * Gönderimden sonra burada yapılan her iş doğrudan t₃ − t₂'ye eklenir. İlk
         * uçtan uca testte (T-10) bakım turu her olaydan sonra koşuyordu ve boş
         * kuyrukta bile t₃ − t₂ = 125 µs ölçüldü; bunun büyük kısmı stack'i tarayan
         * tanı çağrısıydı — yani ölçüm aletinin kendi izi (tasarım §5.2). */
        const TickType_t now = xTaskGetTickCount();
        if (got != pdPASS || (now - last_maint) >= pdMS_TO_TICKS(APP_BUTTON_POLL_MS)) {
            last_maint = now;
            button_rearm_poll();                                 /* FR-12b */
            exp_tick();                                          /* tasarım §7 */

            g_diag.btn_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
            g_diag.task_count    = (uint32_t)uxTaskGetNumberOfTasks();
            g_diag.heap_free     = (uint32_t)xPortGetFreeHeapSize();
            g_diag.heap_min_free = (uint32_t)xPortGetMinimumEverFreeHeapSize();
        }
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

    /* Ek CPU işinin bu derlemedeki hızı (T-16 kalibrasyonunun girdisi). Scheduler
     * öncesi ölçülür; yalnızca HAL tick kesmesi araya girebilir (~1 µs/ms). */
    const uint32_t t = timer_us();
    calibrated_work(100000u);
    g_diag.work_us_per_100k = timer_us() - t;

    uart_link_start_rx();                                        /* FR-80 */
}

void app_create_rtos_objects(void)
{
    g_button_q = xQueueCreate(APP_BUTTON_QUEUE_LEN, sizeof(ButtonEvent));
    g_tx_q     = xQueueCreate(APP_TX_QUEUE_LEN, sizeof(TxMsg));
    configASSERT(g_button_q != NULL && g_tx_q != NULL);

    vQueueAddToRegistry(g_button_q, "buttonQ");
    vQueueAddToRegistry(g_tx_q, "txQ");

    /* Her dönüş değeri pdPASS ile ayrı ayrı karşılaştırılır. Başarısızlık -1 döner;
     * `ok &= xTaskCreate(...)` gibi bit düzeyinde birleştirme 1 & -1 = 1 verip
     * hatayı gizlerdi. */
    const BaseType_t r_tel = xTaskCreate(TelemetryTask, "Telemetry", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_TELEMETRY, NULL);
    const BaseType_t r_btn = xTaskCreate(ButtonTask, "Button", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_BUTTON, NULL);
    const BaseType_t r_tx  = xTaskCreate(UartTxTask, "UartTx", APP_TASK_STACK_WORDS,
                                         NULL, APP_PRIO_UARTTX, NULL);
    configASSERT(r_tel == pdPASS && r_btn == pdPASS && r_tx == pdPASS);
}
