/**
 * @file    app_tasks.h
 * @brief   Uygulama görevleri, kuyruklar ve açılış sırası.
 * @ingroup tasks
 */

/**
 * @defgroup tasks RTOS Görevleri
 * @brief    Üç uygulama görevi ve aralarındaki iki kuyruk (FR-01…06).
 *
 * | Görev           | Öncelik | Bloklandığı yer                          |
 * |-----------------|---------|------------------------------------------|
 * | `TelemetryTask` | 3       | `xTaskDelayUntil` / S0'da `ulTaskNotifyTake` |
 * | `ButtonTask`    | 2       | `xQueueReceive(buttonQ)`                 |
 * | `UartTxTask`    | 1       | `xQueueReceive(txQ)` + TC bildirimi      |
 *
 * Görevler CMSIS katmanı yerine doğrudan FreeRTOS `xTaskCreate()` ile, ödevdeki
 * 3 / 2 / 1 öncelik değerleriyle oluşturulur. (`configMAX_PRIORITIES` CubeMX'te
 * 56'ya kilitli; 4…55 kullanılmaz — tasarım §13.)
 *
 * Hiçbir görev meşgul beklemez; beklemeler daima bloklayıcı RTOS çağrısıdır (FR-06).
 * UART'a yalnızca `UartTxTask` erişir (FR-05).
 */

#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "FreeRTOS.h"
#include "queue.h"

/**
 * @brief   Buton ISR'ından `ButtonTask`'a olay kuyruğu (8 × @ref ButtonEvent, FR-70).
 * @ingroup tasks
 * @par Paylaşılan durum
 *      Yazar: buton ISR'ı. Okur: ButtonTask. Oluşturulana kadar `NULL`'dır.
 */
extern QueueHandle_t g_button_q;

/**
 * @brief   Ortak TX kuyruğu (16 × @ref TxMsg, FIFO, FR-71).
 * @ingroup tasks
 * @par Paylaşılan durum
 *      Yazarlar: TelemetryTask, ButtonTask. Okur: UartTxTask.
 */
extern QueueHandle_t g_tx_q;

/**
 * @brief   Scheduler öncesi hazırlık: zaman kaynağı ve kayıt defteri öz-testi.
 * @ingroup tasks
 * @note    `main()` içinden, çevre birimleri kurulduktan sonra ve `osKernelInitialize()`
 *          çağrısından önce bir kez çağrılır.
 */
void app_init_early(void);

/**
 * @brief   Kuyrukları ve üç uygulama görevini oluşturur.
 * @ingroup tasks
 * @note    `MX_FREERTOS_Init()` içindeki USER CODE bloğundan, scheduler başlamadan
 *          çağrılır. Oluşturma başarısızsa `configASSERT` ile durur — eksik bir görevle
 *          ölçüm yapılmamalıdır.
 */
void app_create_rtos_objects(void);

#endif /* APP_TASKS_H */
