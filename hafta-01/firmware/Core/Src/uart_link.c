/**
 * @file    uart_link.c
 * @brief   USART2 gönderim görevi, TC/DMA/RX kesme geri çağrıları (T-08, T-12).
 * @ingroup proto
 *
 * **TC yolu doğrulaması (R-2, tasarım §4.2):** HAL 1.8.5'te normal modlu DMA
 * gönderiminde `UART_DMATransmitCplt()` geri çağrıyı **çağırmaz**; yalnızca
 * USART `TCIE` kesmesini açar. `HAL_UART_TxCpltCallback()`, `HAL_UART_IRQHandler()`
 * USART_SR_TC bayrağını gördüğünde `UART_EndTransmit_IT()` içinden çağrılır —
 * yani son stop biti hattan çıktıktan sonra. Bu modül ayrıca DMA bitişi ile TC
 * arasındaki süreyi ölçerek bunu çalışma zamanında da kanıtlar.
 */

#include "uart_link.h"

#include "main.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_config.h"
#include "app_diag.h"
#include "app_tasks.h"
#include "app_types.h"
#include "record.h"
#include "timing.h"

#include <string.h>

/* ---------------------------------------------------------------- gönderim */

/**
 * @brief   DMA'nın okuduğu kalıcı gönderim tamponu (FR-45, HW-05).
 *
 * Görev stack'indeki bir yerel değişken DMA sürerken geçersizleşebilirdi.
 * `static` olduğu için `.bss`'te, yani normal SRAM'dedir; CCM RAM'e düşmez
 * (linker raporu: CCMRAM 0 B).
 */
static uint8_t s_tx_buf[APP_TX_BUF_LEN];

/**
 * @brief   Hatta giden mesajın türü ve olay kimliği.
 * @par Paylaşılan durum
 *      Yazar: UartTxTask (DMA başlatılmadan önce). Okur: TC ISR'ı. Aynı anda tek
 *      aktarım olduğu için ISR yalnızca kendi aktarımının değerlerini görür.
 */
static volatile MsgKind  s_tx_kind;
static volatile uint32_t s_tx_event;
static volatile bool     s_tx_busy;

/** @brief UartTxTask'ın tutamacı; TC ISR'ı bununla görevi uyandırır. */
static TaskHandle_t s_tx_task;

/** @brief Son USART2 kesmesinin giriş zamanı [µs] (t₄ kaynağı). Yazar/okur: USART2 ISR. */
static volatile uint32_t s_uart_entry_us;
/** @brief Son TX DMA kesmesinin giriş zamanı [µs]. Yazar: DMA ISR; okur: USART2 ISR. */
static volatile uint32_t s_dma_entry_us;
/** @brief Son BTN mesajının t₃'ü; TC ISR'ı t₄ − t₃'ü tanıya yazar. */
static volatile uint32_t s_tx_t3;

void uart_irq_entry(void)
{
    s_uart_entry_us = timer_us();
}

void uart_dma_tx_irq_entry(void)
{
    s_dma_entry_us = timer_us();
}

bool uart_link_busy(void)
{
    return s_tx_busy;
}

void UartTxTask(void *arg)
{
    (void)arg;
    s_tx_task = xTaskGetCurrentTaskHandle();

    for (;;) {
        TxMsg m;

        /* Kuyruk derinliği yalnızca bu görev tarafından azaltılır; alımdan hemen
         * önceki derinlik, önceki alımdan beri görülen en yüksek değerdir (±1). */
        const UBaseType_t depth = uxQueueMessagesWaiting(g_tx_q);
        if (depth > g_cnt_task.txq_hwm) {
            g_cnt_task.txq_hwm = depth;
        }

        (void)xQueueReceive(g_tx_q, &m, portMAX_DELAY);     /* FR-40: FIFO */

        memcpy(s_tx_buf, m.data, m.len);
        s_tx_kind  = m.kind;
        s_tx_event = m.event_id;
        s_tx_busy  = true;
        (void)ulTaskNotifyTake(pdTRUE, 0);                   /* bayat bildirim kalmasın */

        const uint32_t t3 = timer_us();                      /* t₃ — FR-42 */
        if (m.kind == MSG_BTN) {
            s_tx_t3 = t3;
            if (!rec_stamp(m.event_id, TS_T3, t3)) {
                g_cnt_task.rec_mismatch_tx++;
            }
        }

        if (HAL_UART_Transmit_DMA(&huart2, s_tx_buf, m.len) != HAL_OK) {
            g_cnt_task.uart_err++;                           /* FR-44 */
            if (m.kind == MSG_BTN) {
                (void)rec_close(m.event_id, REC_TX_ERROR);
            }
            s_tx_busy = false;
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0u) {
            g_cnt_task.uart_timeout++;                       /* MR-06: sonsuz bekleme yok */
            (void)HAL_UART_AbortTransmit(&huart2);
            if (m.kind == MSG_BTN) {
                (void)rec_close(m.event_id, REC_TIMEOUT);
            }
        }
        s_tx_busy = false;
    }
}

/**
 * @brief   UART gönderim tamamlandı (TC) geri çağrısı.
 * @param   huart  Kesmeyi üreten UART.
 *
 * @note    **ISR bağlamı** (USART2, öncelik 5).
 * @par Zaman damgası
 *      **t₄** = @ref uart_irq_entry'nin yakaladığı giriş zamanı (FR-43).
 * @par Paylaşılan durum
 *      Yazar: `g_cnt_isr.rec_mismatch`, `g_diag.last_dma_to_tc_us / max_dma_to_tc_us /
 *      last_t4_minus_t3` (yalnızca bu ISR).
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }
    const uint32_t t4 = s_uart_entry_us;

    if (s_tx_kind == MSG_BTN) {
        const uint32_t id = s_tx_event;
        if (!(rec_stamp(id, TS_T4, t4) && rec_close(id, REC_OK))) {
            g_cnt_isr.rec_mismatch++;
        }
        const uint32_t dma_to_tc = t4 - s_dma_entry_us;
        g_diag.last_dma_to_tc_us = dma_to_tc;
        if (dma_to_tc > g_diag.max_dma_to_tc_us) {
            g_diag.max_dma_to_tc_us = dma_to_tc;
        }
        g_diag.last_t4_minus_t3 = t4 - s_tx_t3;
    }

    if (s_tx_task != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_tx_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

/* ------------------------------------------------------------------- alım */

/** @brief Komut satırının azami uzunluğu (LF hariç). */
#define RX_LINE_MAX  31u

static uint8_t       s_rx_byte;                    /**< HAL'in tek baytlık alım tamponu. */
static char          s_rx_line[RX_LINE_MAX + 1u];  /**< Toplanan satır. Yalnızca RX ISR. */
static uint32_t      s_rx_len;                     /**< Yalnızca RX ISR. */
static bool          s_rx_discard;                 /**< Taşan satırın kalanı atılıyor. Yalnızca RX ISR. */

/**
 * @brief   Tamamlanmış komut satırı ve hazır bayrağı.
 * @par Paylaşılan durum
 *      Tek yönlü devir: ISR yalnızca `s_cmd_ready == false` iken `s_cmd_line`'a yazar
 *      ve bayrağı `true` yapar; görev yalnızca `true` iken okur ve `false` yapar.
 *      Bayrak iki tarafa sırayla sahiplik verir; kilit gerekmez.
 */
static char          s_cmd_line[RX_LINE_MAX + 1u];
static volatile bool s_cmd_ready;
static volatile bool s_rx_restart;                 /**< Alım durdu; görev yeniden başlatsın. */

static void rx_arm(void)
{
    if (HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1u) != HAL_OK) {
        s_rx_restart = true;
    }
}

void uart_link_start_rx(void)
{
    rx_arm();
}

/**
 * @brief   Bir bayt alındı: satırı topla, LF'de devret.
 * @param   huart  Kesmeyi üreten UART.
 * @note    **ISR bağlamı.** Yalnızca bayt biriktirir; ayrıştırma görevde yapılır (FR-81).
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }
    const char c = (char)s_rx_byte;

    if (c == '\n') {
        if (!s_rx_discard) {
            if (s_cmd_ready) {
                g_cnt_isr.cmd_busy++;
            } else {
                memcpy(s_cmd_line, s_rx_line, s_rx_len);
                s_cmd_line[s_rx_len] = '\0';
                s_cmd_ready = true;
            }
        }
        s_rx_len = 0u;
        s_rx_discard = false;
    } else if (c != '\r' && !s_rx_discard) {
        if (s_rx_len < RX_LINE_MAX) {
            s_rx_line[s_rx_len++] = c;
        } else {
            g_cnt_isr.rx_overflow++;                     /* FR-83: sessiz yutma yok */
            s_rx_discard = true;
        }
    }
    rx_arm();
}

/**
 * @brief   UART hata geri çağrısı (gürültü, çerçeve, taşma, DMA hatası).
 * @param   huart  Kesmeyi üreten UART.
 * @note    **ISR bağlamı.** Taşma (ORE) alımı durdurur; burada yeniden başlatılır.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }
    g_cnt_isr.rx_err++;
    rx_arm();
}

bool uart_link_take_command(char *dst, size_t n)
{
    if (s_rx_restart) {
        s_rx_restart = false;
        rx_arm();
    }
    if (!s_cmd_ready) {
        return false;
    }
    strncpy(dst, s_cmd_line, n - 1u);
    dst[n - 1u] = '\0';
    s_cmd_ready = false;                                 /* sahipliği ISR'a geri ver */
    return true;
}
