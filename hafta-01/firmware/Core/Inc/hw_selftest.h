/**
 * @file    hw_selftest.h
 * @brief   Donanım öz-testi: UART hattı, LED'ler, kullanıcı butonu ve TIM2 zaman kaynağı.
 * @ingroup selftest
 *
 * Görev listesindeki T-03 (donanım doğrulama) ve T-04 (TIM2 doğrulama) adımlarının
 * kanıtlarını üretir. Ölçüm firmware'inin bir parçası değildir: derleme zamanında
 * `HW_SELFTEST=1` verildiğinde `main()` RTOS'u hiç başlatmadan bu testi koşar.
 *
 * Derleme:
 * @code
 * cmake --preset Debug -DHW_SELFTEST=ON
 * cmake --build --preset Debug
 * @endcode
 *
 * Tüm çıktılar USART2 (115200 8N1) üzerinden `HWTEST,<konu>,...` biçiminde,
 * LF ile sonlanan ASCII satırlar olarak gönderilir.
 */

/**
 * @defgroup selftest Donanım Öz-Testi
 * @brief    Kurulumun ve kablolamanın ölçümden önce doğrulanması.
 */

#ifndef HW_SELFTEST_H
#define HW_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   1 ise `main()` RTOS yerine donanım öz-testini koşar.
 * @ingroup selftest
 *
 * Değer CMake seçeneğinden (`-DHW_SELFTEST=ON`) gelir. Varsayılan 0'dır; ölçüm
 * firmware'i bu makro 0 iken derlenmelidir.
 */
#ifndef HW_SELFTEST
#define HW_SELFTEST 0
#endif

/**
 * @brief   Donanım öz-testini çalıştırır. **Geri dönmez.**
 * @ingroup selftest
 *
 * Sırasıyla: açılış bilgisi (saat frekansları), LED sırası, TIM2 doğruluk testi
 * (T-04), ardından sonsuz buton izleme döngüsü (T-03).
 *
 * @note    **Görev bağlamı dışında**, `main()` içinden, FreeRTOS başlamadan önce
 *          çağrılmalıdır. `HAL_Delay()` ve bloklayıcı `HAL_UART_Transmit()`
 *          kullanır; bunlar yalnızca bu tanı aracında kabul edilebilir, ölçüm
 *          yolunda kullanılmaz (FR-41).
 * @pre     `MX_GPIO_Init()`, `MX_USART2_UART_Init()` ve `MX_TIM2_Init()` çağrılmış olmalı.
 */
void hw_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* HW_SELFTEST_H */
