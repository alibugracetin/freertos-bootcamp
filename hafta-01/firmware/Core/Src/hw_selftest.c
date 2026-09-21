/**
 * @file    hw_selftest.c
 * @brief   Donanım öz-testinin gerçekleştirimi (T-03, T-04).
 * @ingroup selftest
 *
 * Bu dosyanın tamamı `HW_SELFTEST == 1` iken derlenir. Ölçüm firmware'inde
 * (makro 0) hiçbir sembol üretmez; özellikle `HAL_GPIO_EXTI_Callback()`
 * tanımı ölçüm koduyla çakışmaz.
 */

#include "hw_selftest.h"

#if HW_SELFTEST

#include "main.h"
#include "usart.h"
#include "tim.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** @addtogroup selftest
 *  @{
 */

/** @brief Heartbeat adımları arasındaki süre [ms]; her adımda sıradaki LED yanar. */
#define ST_ALIVE_PERIOD_MS   1000u
/** @brief Buton seviyesinin yoklanma aralığı [ms]. */
#define ST_BTN_POLL_MS       5u
/** @brief TIM2 doğruluk testinde tekrar sayısı. */
#define ST_TIM_ROUNDS        3u

/**
 * @brief   EXTI0 kesmesinin saydığı yükselen kenar sayısı.
 *
 * @par Paylaşılan durum
 *      Yazar: `HAL_GPIO_EXTI_Callback()` (**ISR bağlamı**).
 *      Okur: `hw_selftest_run()` (ana döngü). 32-bit hizalı okuma Cortex-M4'te
 *      atomik olduğu için kilit gerekmez.
 */
static volatile uint32_t s_exti_edges;

/**
 * @brief   Serbest çalışan 1 MHz sayacın anlık değeri.
 * @return  TIM2 sayaç değeri [µs], 2³² µs'de (≈71.6 dk) sarar.
 * @note    ISR ve görev bağlamında güvenle çağrılabilir: tek bir hizalı 32-bit
 *          register okumasıdır (tasarım §2.1, FR-73).
 */
static inline uint32_t st_timer_us(void)
{
    return TIM2->CNT;
}

/**
 * @brief   Biçimli bir satırı USART2'den bloklayıcı olarak gönderir.
 * @param   fmt  `printf` biçim dizgesi.
 * @note    Yalnızca tanı amaçlıdır. Ölçüm yolunda UART'a yalnızca `UartTxTask`
 *          DMA ile erişir (FR-05, FR-41).
 */
static void st_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof buf) {
        n = (int)sizeof buf - 1;   /* tanı çıktısında kesme kabul edilebilir */
    }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)n, 100u);
}

/**
 * @brief   DWT çevrim sayacını başlatır.
 *
 * DWT->CYCCNT çekirdek saatiyle (HCLK, 168 MHz) sayar. TIM2 ise APB1 timer
 * saatiyle (84 MHz, ÷84) sayar. İki sayacın farklı saat yollarından beslenmesi,
 * TIM2 ön-bölücüsündeki bir hatanın bağımsız olarak yakalanmasını sağlar.
 */
static void st_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * @brief   Dört kullanıcı LED'ini sırayla yakar (HW doğrulaması, T-03).
 */
static void st_led_chase(void)
{
    static const struct { GPIO_TypeDef *port; uint16_t pin; const char *name; } leds[] = {
        { LED_GREEN_GPIO_Port,  LED_GREEN_Pin,  "GREEN"  },
        { LED_ORANGE_GPIO_Port, LED_ORANGE_Pin, "ORANGE" },
        { LED_RED_GPIO_Port,    LED_RED_Pin,    "RED"    },
        { LED_BLUE_GPIO_Port,   LED_BLUE_Pin,   "BLUE"   },
    };

    for (unsigned i = 0; i < sizeof leds / sizeof leds[0]; i++) {
        HAL_GPIO_WritePin(leds[i].port, leds[i].pin, GPIO_PIN_SET);
        st_printf("HWTEST,LED,%s\n", leds[i].name);
        HAL_Delay(300u);
        HAL_GPIO_WritePin(leds[i].port, leds[i].pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief   TIM2'nin gerçekten 1 MHz saydığını doğrular (T-04, FR-72).
 *
 * Her turda aynı aralık iki bağımsız sayaçla ölçülür:
 * - TIM2 (APB1 timer saati 84 MHz, PSC = 83) → µs
 * - DWT->CYCCNT (HCLK 168 MHz) → çevrim / 168 = µs
 *
 * İki değerin birkaç µs içinde eşleşmesi, ön-bölücü ve saat ağacının doğru
 * kurulduğunu gösterir. Mutlak değerin ≈1 001 000 µs çıkması beklenir:
 * `HAL_Delay(n)` tam turu garanti etmek için n+1 tick bekler.
 *
 * @par Zaman damgası
 *      Ölçüm damgası alınmaz; yalnızca zaman kaynağının doğruluğu sınanır.
 */
static void st_timer_check(void)
{
    const uint32_t hclk_mhz = HAL_RCC_GetHCLKFreq() / 1000000u;

    for (uint32_t r = 1; r <= ST_TIM_ROUNDS; r++) {
        uint32_t t0 = st_timer_us();
        uint32_t c0 = DWT->CYCCNT;
        HAL_Delay(1000u);
        uint32_t t1 = st_timer_us();
        uint32_t c1 = DWT->CYCCNT;

        uint32_t tim_us = t1 - t0;                 /* mod 2^32, FR-68 */
        uint32_t dwt_us = (c1 - c0) / hclk_mhz;
        st_printf("HWTEST,TIM,round=%lu,tim2_us=%lu,dwt_us=%lu,diff_us=%ld\n",
                  (unsigned long)r, (unsigned long)tim_us, (unsigned long)dwt_us,
                  (long)((int32_t)(tim_us - dwt_us)));
    }

    /* İşaretsiz çıkarmanın sarmayı doğru işlediğinin sayısal kanıtı (FR-68):
     * sayaç 0xFFFFFFF0'da başlayıp 0x00000010'a sarmış olsun → gerçek aralık 32 µs. */
    const uint32_t before = 0xFFFFFFF0u;
    const uint32_t after  = 0x00000010u;
    st_printf("HWTEST,WRAP,before=0x%08lX,after=0x%08lX,delta=%lu,expected=32\n",
              (unsigned long)before, (unsigned long)after, (unsigned long)(after - before));
}

/**
 * @brief   EXTI hattı kesme geri çağrısı; butonun yükselen kenarlarını sayar.
 * @param   GPIO_Pin  Kesmeyi üreten pin.
 *
 * @note    **ISR bağlamında** çalışır (EXTI0_IRQHandler → HAL_GPIO_EXTI_IRQHandler).
 *          Tek bir basışta birden fazla kenar sayılması, butonun zıpladığını
 *          (bounce) ve FR-12'deki 30 ms filtresinin neden gerektiğini gösterir.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == BTN_USER_Pin) {
        s_exti_edges++;
    }
}

void hw_selftest_run(void)
{
    st_dwt_init();
    (void)HAL_TIM_Base_Start(&htim2);   /* TIM2 başlatılmadan CNT sabit kalır */

    st_printf("\nHWTEST,BOOT,SYSCLK=%lu,HCLK=%lu,PCLK1=%lu,PCLK2=%lu\n",
              (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HAL_RCC_GetHCLKFreq(),
              (unsigned long)HAL_RCC_GetPCLK1Freq(),   (unsigned long)HAL_RCC_GetPCLK2Freq());

    st_led_chase();
    st_timer_check();

    st_printf("HWTEST,INFO,btn_level_idle=%u,press the blue button\n",
              (unsigned)HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin));

    GPIO_PinState last_level   = HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin);
    /* Kenarlar bir bırakıştan sonrakine kadar sayılır. Anlık görüntüyü basışta
     * almak yanlış olurdu: yoklama basışı ≤5 ms geç fark eder ve kesme o arada
     * ilk kenarı ve zıplamaların bir kısmını çoktan saymış olur. */
    uint32_t      edges_at_release = s_exti_edges;
    uint32_t      presses      = 0;
    uint32_t      alive        = 0;
    uint32_t      last_alive   = HAL_GetTick();

    for (;;) {
        GPIO_PinState level = HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin);

        if (level != last_level) {
            if (level == GPIO_PIN_SET) {
                presses++;
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
                st_printf("HWTEST,BTN,press=%lu,level=1,t_us=%lu\n",
                          (unsigned long)presses, (unsigned long)st_timer_us());
            } else {
                uint32_t now_edges = s_exti_edges;
                HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
                /* 1 = temiz basış; >1 = zıplama (bounce) var → FR-12 filtresi gerekli */
                st_printf("HWTEST,BTN,release=%lu,level=0,rising_edges_this_cycle=%lu\n",
                          (unsigned long)presses, (unsigned long)(now_edges - edges_at_release));
                edges_at_release = now_edges;
            }
            last_level = level;
        }

        if (HAL_GetTick() - last_alive >= ST_ALIVE_PERIOD_MS) {
            /* Turuncu → kırmızı → mavi sürekli döner; kullanıcı LED'leri kendi
             * hızında gözle doğrulayabilir. Yeşil butona ayrılmıştır. */
            static const struct { uint16_t pin; const char *name; } ring[] = {
                { LED_ORANGE_Pin, "ORANGE" }, { LED_RED_Pin, "RED" }, { LED_BLUE_Pin, "BLUE" },
            };
            const unsigned k = alive % (sizeof ring / sizeof ring[0]);

            last_alive += ST_ALIVE_PERIOD_MS;
            HAL_GPIO_WritePin(GPIOD, LED_ORANGE_Pin | LED_RED_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOD, ring[k].pin, GPIO_PIN_SET);
            st_printf("HWTEST,ALIVE,n=%lu,led=%s,t_us=%lu,exti_edges=%lu\n",
                      (unsigned long)++alive, ring[k].name, (unsigned long)st_timer_us(),
                      (unsigned long)s_exti_edges);
        }

        HAL_Delay(ST_BTN_POLL_MS);
    }
}

/** @} */

#endif /* HW_SELFTEST */
