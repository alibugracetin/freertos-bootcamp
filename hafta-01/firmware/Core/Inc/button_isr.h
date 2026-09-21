/**
 * @file    button_isr.h
 * @brief   Kullanıcı butonu kesmesi: t₀, tekrar-kenar filtresi, olay kuyruğu.
 * @ingroup isr
 */

/**
 * @defgroup isr Kesme Servis Rutinleri
 * @brief    Kısa tutulan, beklemeyen, UART'a yazmayan kesmeler (FR-16).
 *
 * Tüm uygulama kesmeleri NVIC önceliği **5**'tedir: FreeRTOS'un `...FromISR()`
 * API'lerini çağırabilen en yüksek seviye (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`).
 * Eşit öncelikteki kesmeler birbirini kesemez; bir buton basışı önceki olayın
 * t₄ ölçümünü bölemez (tasarım §2.2).
 */

#ifndef BUTTON_ISR_H
#define BUTTON_ISR_H

#include <stdint.h>

/**
 * @brief   EXTI0 kesmesinin **ilk** işi: t₀'ı yakalar.
 * @ingroup isr
 *
 * `stm32f4xx_it.c` içindeki `EXTI0_IRQHandler()`'ın, HAL'in bayrağı temizleyip
 * geri çağrıyı çalıştırmasından **önceki** USER CODE bloğundan çağrılır.
 *
 * @note    **ISR bağlamı.** Yalnızca bir register okur ve bir değişkene yazar.
 * @par Zaman damgası
 *      **t₀** burada okunur (FR-11): EXTI bayrağı temizlenmeden önce, kenara
 *      mümkün olan en yakın anda. Filtre bu değeri daha sonra kullanır.
 */
void button_irq_entry(void);

/**
 * @brief   Buton bırakılmış görünüyorsa filtreyi yeniden silahlandırır (FR-12b).
 * @ingroup isr
 *
 * Pin, ilk LOW gözleminden itibaren @ref APP_REARM_LOW_US boyunca her çağrıda
 * LOW okunduysa silah açılır. Arada HIGH okunursa süre baştan başlar.
 *
 * @note    **Görev bağlamı** — ButtonTask'tan en az @ref APP_BUTTON_POLL_MS
 *          aralıklarla çağrılır. ISR'dan çağrılmamalıdır.
 * @note    Silahı açma kısa bir kritik bölüm içinde, pin yeniden okunarak yapılır;
 *          aynı anda gelen bir basış kenarı kaybolmaz (tasarım §4.1.1).
 * @par Paylaşılan durum
 *      Silah bayrağını yazar: bu fonksiyon (açar) ve buton ISR'ı (kapatır).
 */
void button_rearm_poll(void);

#endif /* BUTTON_ISR_H */
