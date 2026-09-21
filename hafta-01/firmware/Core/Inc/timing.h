/**
 * @file    timing.h
 * @brief   Mikrosaniye zaman kaynağı (TIM2, 1 MHz, 32-bit serbest sayaç).
 * @ingroup timing
 */

/**
 * @defgroup timing Zaman Kaynağı ve Damgalar
 * @brief    t₀…t₄ damgalarının alındığı tek saat.
 *
 * Tüm damgalar aynı kart saatinden alınır; PC saatiyle birleştirilmez (UI-11).
 * TIM2, APB1 timer saatini (84 MHz) 84'e bölerek 1 µs çözünürlükle sayar ve
 * 2³² µs'de (≈71.6 dakika) sarar. Doğruluğu T-04'te DWT çevrim sayacıyla
 * 1 ppm içinde doğrulanmıştır (tasarım §2.1).
 */

#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>
#include "stm32f4xx.h"

/**
 * @brief   Serbest çalışan 1 MHz sayacın anlık değeri.
 * @ingroup timing
 * @return  Zaman [µs], mod 2³².
 *
 * @note    ISR ve görev bağlamında güvenle çağrılabilir: tek bir hizalı 32-bit
 *          register okumasıdır, Cortex-M4'te atomiktir (FR-73). HAL sarmalayıcısı
 *          kullanılmaz; ölçüm noktalarına çağrı yükü eklenmesin diye.
 * @note    İki damga arasındaki süre daima işaretsiz çıkarmayla hesaplanır:
 *          `uint32_t d = t_son - t_ilk;` sayaç arada sarsa bile doğrudur (FR-68).
 */
static inline uint32_t timer_us(void)
{
    return TIM2->CNT;
}

/**
 * @brief   TIM2 sayacını başlatır.
 * @ingroup timing
 *
 * `MX_TIM2_Init()` timer'ı yalnızca yapılandırır; bu çağrı olmadan `TIM2->CNT`
 * sıfırda kalır ve tüm damgalar 0 çıkar (tasarım §2.1).
 *
 * @pre     `MX_TIM2_Init()` çağrılmış olmalı.
 * @note    Scheduler başlamadan önce, `main()` içinden bir kez çağrılır.
 */
void timing_init(void);

#endif /* TIMING_H */
