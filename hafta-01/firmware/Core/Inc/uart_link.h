/**
 * @file    uart_link.h
 * @brief   USART2'nin tek sahibi: DMA ile gönderim (t₃, t₄) ve komut alma.
 * @ingroup proto
 *
 * UART'a yalnızca bu modül erişir (FR-05). Gönderim `UartTxTask` içinde,
 * alım USART2 kesmesinde yapılır. Diğer görevler yalnızca `txQ`'ya mesaj koyar.
 */

#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief   UART gönderim görevi — **düşük öncelik (1)**.
 * @ingroup tasks
 * @param   arg  Kullanılmıyor.
 *
 * `txQ`'dan FIFO sırayla bir mesaj alır, kalıcı tampona kopyalar, DMA ile gönderir
 * ve UART **TC** (son bit hattan çıktı) bildirimi gelene kadar bloklanır (FR-41).
 *
 * @par Zaman damgası
 *      **t₃** — `HAL_UART_Transmit_DMA()` çağrısından hemen önce (FR-42). Kayda
 *      başlatmadan **önce** yazılır; aksi halde TC kesmesi kaydı kapatmaya
 *      çalıştığında t₃ henüz yazılmamış olabilirdi.
 */
void UartTxTask(void *arg);

/**
 * @brief   USART2 kesmesinin ilk işi: giriş zamanını yakalar.
 * @ingroup isr
 * @note    **ISR bağlamı.** `USART2_IRQHandler()`'ın USER CODE 0 bloğundan çağrılır.
 * @par Zaman damgası
 *      **t₄** bu değerdir (FR-43): HAL'in TC dalını işlemesinden önce, TC'ye en
 *      yakın gözlem anı. RX kesmelerinde de alınır ama kullanılmaz.
 */
void uart_irq_entry(void);

/**
 * @brief   TX DMA kesmesinin ilk işi: giriş zamanını yakalar (R-2 doğrulaması).
 * @ingroup isr
 * @note    **ISR bağlamı.** `DMA1_Stream6_IRQHandler()`'ın USER CODE 0 bloğundan çağrılır.
 *          Yarı-transfer ve tam-transfer kesmelerinde ayrı ayrı çalışır; son değer
 *          tam-transferin zamanıdır. t₄ ile farkı `g_diag.last_dma_to_tc_us`'e yazılır.
 */
void uart_dma_tx_irq_entry(void);

/**
 * @brief   Komut alımını başlatır (bayt bayt, kesmeyle, FR-80).
 * @ingroup proto
 * @note    Scheduler başlamadan önce bir kez çağrılır.
 */
void uart_link_start_rx(void);

/**
 * @brief   Bekleyen tam bir komut satırı varsa kopyalar.
 * @ingroup proto
 * @param   dst  Hedef tampon.
 * @param   n    Tampon boyutu.
 * @return  `true`: `dst` içinde NUL ile biten, LF'siz bir satır var.
 * @note    **Görev bağlamı** (ButtonTask bakım turu). Ayrıştırma ISR'da yapılmaz.
 *          Alım durmuşsa (UART hatası sonrası) burada yeniden başlatılır.
 */
bool uart_link_take_command(char *dst, size_t n);

/**
 * @brief   Şu an hatta giden bir aktarım var mı.
 * @ingroup proto
 * @return  `true`: DMA başlatıldı, TC henüz gelmedi.
 */
bool uart_link_busy(void);

#endif /* UART_LINK_H */
