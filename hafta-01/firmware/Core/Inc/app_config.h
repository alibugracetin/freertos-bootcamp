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
 * TEL/BTN tam @ref APP_MSG_LEN bayt kullanır. Fazlası yalnızca deney dışındaki
 * kontrol satırları içindir: en uzunu `CNI` sayaç satırı, en kötü durumda
 * 4 + 11 × 10 hane + 10 virgül + LF = 125 bayt (tasarım §3).
 */
#define APP_TX_BUF_LEN         128u

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

/** @brief Deney başında ısınma süresi [ms] (MR-03, FR-87). */
#define APP_WARMUP_MS          5000u

/** @brief `CMD,START` sayı verilmezse hedef olay sayısı (MR-01). */
#define APP_DEFAULT_TARGET     30u

/** @brief DRAINING durumunda kapanmayan kayıtları `timeout` sayma süresi [ms] (MR-06). */
#define APP_DRAIN_TIMEOUT_MS   1000u

/**
 * @brief S4 için ek CPU işi [iterasyon] — hedef ≈ 2 ms (FR-23).
 *
 * **Kalibrasyon (T-16, 23.09.2026, Debug -O0).** İki ölçüm alındı:
 * - Açılışta, scheduler öncesi: `calibrated_work(100 000)` = 14 305 µs → 143,1 ns/iterasyon
 * - Koşan deneyde (S5 denemesi, TST satırı): 34 953 iterasyon = 4 383 µs → **125,4 ns/iterasyon**
 *
 * Aradaki %14 fark flash önbelleğinden (ART) gelir: açılıştaki ilk çalıştırma
 * soğuk önbellekle, deneydeki tekrarlar sıcak önbellekle koşar. Kalibrasyon
 * **deney koşullarındaki** değerle yapılır; açılış ölçümü yalnızca kaba bir
 * kontroldür. Derleme ayarı değişirse (ör. -O2) bu sabitler geçersizdir.
 * Gerçekleşen iş süresi her koşuda TST satırında raporlanır (FR-25).
 */
#define APP_WORK_ITERS_S4      15949u
/** @brief S5 için ek CPU işi [iterasyon] — hedef ≈ 5 ms: 5 000 / 0,1254 ≈ 39 873. Bkz. @ref APP_WORK_ITERS_S4. */
#define APP_WORK_ITERS_S5      39873u

#endif /* APP_CONFIG_H */
