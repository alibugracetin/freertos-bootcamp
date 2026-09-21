/**
 * @file    app_config.h
 * @brief   Uygulama genelindeki sabit parametreler (ödev standardı).
 * @ingroup tasks
 *
 * Senaryolar arasında **değiştirilmemesi gereken** değerler burada toplanır
 * (MR-07). Buradaki bir değeri değiştirmek, önceki ölçümlerle
 * karşılaştırılabilirliği bozar; gerekirse sapma `README.md`'de gerekçelendirilir.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/** @brief `buttonQ` kapasitesi [olay] (FR-70). */
#define APP_BUTTON_QUEUE_LEN   8u

/** @brief `txQ` kapasitesi [mesaj], FIFO (FR-71). */
#define APP_TX_QUEUE_LEN       16u

/** @brief TEL ve BTN mesajlarının sabit uzunluğu [bayt], sondaki LF dahil (FR-50). */
#define APP_MSG_LEN            64u

/**
 * @brief TX mesaj tamponu [bayt].
 *
 * TEL/BTN tam @ref APP_MSG_LEN bayt kullanır. Fazlası yalnızca deney sonundaki
 * `REC` döküm satırı içindir (en kötü durum 83 bayt; tasarım §3).
 */
#define APP_TX_BUF_LEN         96u

/** @brief Tekrar-kenar filtresi penceresi [µs] (FR-12). */
#define APP_DEBOUNCE_US        30000u

/**
 * @brief Yeniden silahlanma için butonun kesintisiz bırakılmış görülmesi gereken süre [µs] (FR-12b).
 *
 * Bırakış zıplamasının hayalet basış üretmesini engeller (tasarım §4.1.1).
 */
#define APP_REARM_LOW_US       30000u

/**
 * @brief ButtonTask'ın olay yokken uyanma aralığı [ms].
 *
 * Silah kontrolü (FR-12b) ve deney durum makinesi (tasarım §7) bu turda çalışır.
 */
#define APP_BUTTON_POLL_MS     50u

/** @brief Ölçüm kayıt havuzu kapasitesi [olay] (FR-60). */
#define APP_REC_POOL_SIZE      64u

/** @brief `TelemetryTask` önceliği — yüksek (FR-02). */
#define APP_PRIO_TELEMETRY     3u
/** @brief `ButtonTask` önceliği — orta (FR-02). */
#define APP_PRIO_BUTTON        2u
/** @brief `UartTxTask` önceliği — düşük (FR-02). */
#define APP_PRIO_UARTTX        1u

/** @brief Her uygulama görevinin stack boyutu [word = 4 bayt]. */
#define APP_TASK_STACK_WORDS   512u

#endif /* APP_CONFIG_H */
