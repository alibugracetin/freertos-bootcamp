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
 * gerekmez (FR-24). Sıfırlama yalnızca senaryo değişiminde, ölçüm dışında ve kısa
 * bir kritik bölüm içinde yapılır.
 *
 * @ref g_diag, hata ayıklayıcı veya `tools/read_diag.py` ile karta dokunmadan
 * (UART trafiği eklemeden) okunabilen bir durum özetidir.
 *
 * Alan sıraları sabittir; `tools/read_diag.py` yapıları 32-bit kelimeler olarak
 * okur. Yeni alan yalnızca **sona** eklenir ve betiğe de yazılır.
 */

#ifndef APP_DIAG_H
#define APP_DIAG_H

#include <stdint.h>

/**
 * @brief   Yalnızca ISR'ların yazdığı sayaçlar.
 * @ingroup diag
 */
typedef struct {
    uint32_t accepted;       /**< Ölçümde kabul edilip `buttonQ`'ya yazılan olay (buton ISR). */
    uint32_t debounce_rej;   /**< 30 ms içindeki tekrar kenar (buton ISR, FR-12). */
    uint32_t btnq_drop;      /**< `buttonQ` dolu (buton ISR, FR-15). */
    uint32_t rec_ovf;        /**< Kayıt açılamadı: slot dolu (buton ISR, FR-61). */
    uint32_t rec_mismatch;   /**< TC ISR'ı t₄'ü kayda yazamadı (UART TC ISR). */
    uint32_t unarmed_rej;    /**< Silah kapalıyken gelen kenar (buton ISR, FR-12b). */
    uint32_t events;         /**< Ölçümde kimlik atanan olay = accepted + btnq_drop + rec_ovf (buton ISR). */
    uint32_t idle_press;     /**< Ölçüm dışında (IDLE/WARMUP…) filtreden geçen basış (buton ISR). */
    uint32_t rx_overflow;    /**< Komut satırı tampona sığmadı (UART RX ISR, FR-83). */
    uint32_t rx_err;         /**< UART hata geri çağrısı: gürültü, çerçeve, taşma (UART ISR). */
    uint32_t cmd_busy;       /**< Önceki komut işlenmeden yenisi geldi, yenisi atıldı (UART RX ISR). */
} IsrCounters;

/**
 * @brief   Yalnızca görevlerin yazdığı sayaçlar. Her alanın tek yazar görevi vardır.
 * @ingroup diag
 */
typedef struct {
    uint32_t txq_drop_tel;   /**< `txQ` dolu, telemetri düştü (TelemetryTask). */
    uint32_t txq_drop_btn;   /**< `txQ` dolu, yanıt düştü (ButtonTask, FR-34). */
    uint32_t rec_mismatch;   /**< t₁/t₂ kayda yazılamadı (ButtonTask). */
    uint32_t uart_err;       /**< Gönderim başlatılamadı (UartTxTask, FR-44). */
    uint32_t uart_timeout;   /**< TC 1 s içinde gelmedi (UartTxTask, MR-06). */
    uint32_t rec_mismatch_tx;/**< t₃ kayda yazılamadı (UartTxTask). */
    uint32_t fmt_err_tel;    /**< TEL içeriği 63 baytı aştı (TelemetryTask, FR-52). */
    uint32_t fmt_err_btn;    /**< BTN içeriği 63 baytı aştı (ButtonTask, FR-52). */
    uint32_t txq_hwm;        /**< `txQ` yüksek su seviyesi, ±1 (UartTxTask, FR-65). */
    uint32_t cmd_err;        /**< Geçersiz veya yanlış durumda gelen komut (ButtonTask, FR-83). */
} TaskCounters;

/**
 * @brief   Karta dokunmadan okunabilen durum özeti.
 * @ingroup diag
 */
typedef struct {
    uint32_t magic;              /**< [0] @ref APP_DIAG_MAGIC. */
    uint32_t selfcheck;          /**< [1] 0 = koşmadı, 1 = geçti, 2 = kaldı (T-05). */
    uint32_t selfcheck_opened;   /**< [2] 100 sahte olaydan açılan (beklenen: 64). */
    uint32_t selfcheck_rejected; /**< [3] Taşma nedeniyle reddedilen (beklenen: 36). */
    uint32_t task_count;         /**< [4] Beklenen: 4 = 3 + Idle. */
    uint32_t heap_free;          /**< [5] Boş heap [bayt]. */
    uint32_t heap_min_free;      /**< [6] Açılıştan beri en düşük boş heap [bayt]. */
    uint32_t btn_wakeups;        /**< [7] ButtonTask'ın olay aldığı sayı. */
    uint32_t last_event_id;      /**< [8] Son olay kimliği. */
    uint32_t last_t0;            /**< [9] Son olayın t₀'ı [µs]. */
    uint32_t last_t1;            /**< [10] Son olayın t₁'i [µs]. */
    uint32_t last_t1_minus_t0;   /**< [11] Son olayda ISR → görev [µs]. */
    uint32_t max_t1_minus_t0;    /**< [12] En büyük ISR → görev [µs]. */
    uint32_t fault;              /**< [13] 0 = yok, 1 = stack taşması, 2 = heap tükendi. */
    uint32_t btn_stack_hwm;      /**< [14] ButtonTask stack'inde en az boş alan [word]. */
    uint32_t work_us_per_100k;   /**< [15] calibrated_work(100 000) süresi [µs], açılışta ölçülür. */
    uint32_t last_dma_to_tc_us;  /**< [16] Son BTN'de DMA bitişi → UART TC [µs] (R-2 kanıtı; yazar: TC ISR). */
    uint32_t max_dma_to_tc_us;   /**< [17] En büyük DMA → TC [µs] (yazar: TC ISR). */
    uint32_t last_t4_minus_t3;   /**< [18] Son BTN'de UART başlatma → TC [µs] (yazar: TC ISR). */
} AppDiag;

/** @brief @ref AppDiag::magic değeri. */
#define APP_DIAG_MAGIC  0xD1A60002u

/** @brief ISR sayaçları. @par Paylaşılan durum Yazar: ISR'lar (alan başına tek). */
extern volatile IsrCounters g_cnt_isr;
/** @brief Görev sayaçları. @par Paylaşılan durum Yazar: alan başına tek görev. */
extern volatile TaskCounters g_cnt_task;
/** @brief Tanı özeti. @par Paylaşılan durum Alan açıklamalarında yazar belirtilmiştir. */
extern volatile AppDiag g_diag;

#endif /* APP_DIAG_H */
