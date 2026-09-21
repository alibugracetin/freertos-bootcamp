/**
 * @file    app_diag.h
 * @brief   Hata sayaçları ve SWD üzerinden okunabilen tanı yapısı.
 * @ingroup diag
 */

/**
 * @defgroup diag Sayaçlar ve Tanı
 * @brief    Kayıpları gizlemeden saymak (FR-64, MR-09).
 *
 * **Tek yazar kuralı (tasarım §3.1):** Her sayaç alanını yalnızca **bir** bağlam
 * yazar. ISR'ların yazdıkları @ref g_cnt_isr içinde, görevlerinkiler
 * @ref g_cnt_task içindedir. Böylece `sayac++` gibi atomik olmayan
 * oku-değiştir-yaz işlemleri yarışa girmez ve ölçüm yolunda kesmeleri kapatmak
 * gerekmez (FR-24). İki yapı yalnızca dışa aktarımda, ölçüm durmuşken okunur.
 *
 * @ref g_diag, hata ayıklayıcı veya `STM32_Programmer_CLI -r32` ile karta
 * dokunmadan (UART trafiği eklemeden) okunabilen bir durum özetidir.
 */

#ifndef APP_DIAG_H
#define APP_DIAG_H

#include <stdint.h>

/**
 * @brief   Yalnızca ISR'ların yazdığı sayaçlar.
 * @ingroup diag
 */
typedef struct {
    uint32_t accepted;       /**< Filtreden geçip `buttonQ`'ya yazılan olay (buton ISR). */
    uint32_t debounce_rej;   /**< 30 ms içindeki tekrar kenar (buton ISR, FR-12). */
    uint32_t btnq_drop;      /**< `buttonQ` dolu (buton ISR, FR-15). */
    uint32_t rec_ovf;        /**< Kayıt açılamadı: slot dolu (buton ISR, FR-61). */
    uint32_t rec_mismatch;   /**< TC ISR'ı kaydı bulamadı (T-08'de kullanılacak). */
    uint32_t unarmed_rej;    /**< Silah kapalıyken gelen kenar: bırakış zıplaması veya basılı tutma (buton ISR, FR-12b). */
} IsrCounters;

/**
 * @brief   Yalnızca görevlerin yazdığı sayaçlar. Her alanın tek yazar görevi vardır.
 * @ingroup diag
 */
typedef struct {
    uint32_t txq_drop_tel;   /**< `txQ` dolu, telemetri düştü (yazar: TelemetryTask). */
    uint32_t txq_drop_btn;   /**< `txQ` dolu, yanıt düştü (yazar: ButtonTask, FR-34). */
    uint32_t rec_mismatch;   /**< Damga yazılamadı (yazar: ButtonTask). */
    uint32_t uart_err;       /**< Gönderim başlatılamadı (yazar: UartTxTask, FR-44). */
    uint32_t uart_timeout;   /**< TC 1 s içinde gelmedi (yazar: UartTxTask, MR-06). */
} TaskCounters;

/**
 * @brief   Karta dokunmadan okunabilen durum özeti.
 * @ingroup diag
 *
 * Alan sırası sabittir; PC tarafı bu yapıyı 32-bit kelimeler olarak okur.
 * Yeni alan yalnızca **sona** eklenir.
 */
typedef struct {
    uint32_t magic;              /**< [0] @ref APP_DIAG_MAGIC — yapı bulundu mu? */
    uint32_t selfcheck;          /**< [1] 0 = koşmadı, 1 = geçti, 2 = kaldı (T-05). */
    uint32_t selfcheck_opened;   /**< [2] 100 sahte olaydan açılan (beklenen: 64). */
    uint32_t selfcheck_rejected; /**< [3] Taşma nedeniyle reddedilen (beklenen: 36). */
    uint32_t task_count;         /**< [4] `uxTaskGetNumberOfTasks()` (beklenen: 4 = 3 + Idle). */
    uint32_t heap_free;          /**< [5] Görevler oluşturulduktan sonra boş heap [bayt]. */
    uint32_t heap_min_free;      /**< [6] Açılıştan beri en düşük boş heap [bayt]. */
    uint32_t btn_wakeups;        /**< [7] ButtonTask'ın olay aldığı sayı. */
    uint32_t last_event_id;      /**< [8] Son alınan olay kimliği. */
    uint32_t last_t0;            /**< [9] Son olayın t₀'ı [µs]. */
    uint32_t last_t1;            /**< [10] Son olayın t₁'i [µs]. */
    uint32_t last_t1_minus_t0;   /**< [11] Son olayda ISR → görev süresi [µs]. */
    uint32_t max_t1_minus_t0;    /**< [12] Gözlenen en büyük ISR → görev süresi [µs]. */
    uint32_t fault;              /**< [13] 0 = yok, 1 = stack taşması, 2 = heap tükendi. */
    uint32_t btn_stack_hwm;      /**< [14] ButtonTask stack'inde hiç kullanılmamış en az alan [word]. */
} AppDiag;

/** @brief @ref AppDiag::magic değeri. */
#define APP_DIAG_MAGIC  0xD1A60001u

/** @brief ISR sayaçları. @par Paylaşılan durum Yazar: ISR'lar. Okur: dışa aktarım. */
extern volatile IsrCounters g_cnt_isr;
/** @brief Görev sayaçları. @par Paylaşılan durum Yazar: alan başına tek görev. Okur: dışa aktarım. */
extern volatile TaskCounters g_cnt_task;
/** @brief Tanı özeti. @par Paylaşılan durum Yazar: ButtonTask ve açılış kodu. Okur: SWD. */
extern volatile AppDiag g_diag;

#endif /* APP_DIAG_H */
