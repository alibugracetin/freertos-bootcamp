/**
 * @file    experiment.h
 * @brief   Senaryolar (S0–S5), deney durum makinesi ve TelemetryTask.
 * @ingroup expfsm
 */

/**
 * @defgroup expfsm Deney Durum Makinesi
 * @brief    Senaryo seçimi, ısınma, ölçüm, boşaltma ve döküm (tasarım §7).
 *
 * ```
 *  IDLE ──SCEN──► ARMED ──START──► WARMUP ─5 s─► MEASURING ─n olay─► DRAINING ─► DONE
 *    ▲                                                                              │
 *    └───────────────────────────── STOP (her durumdan) ◄──────────── SCEN / DUMP ──┘
 * ```
 *
 * Durum makinesi **ayrı bir görevde değil**, ButtonTask'ın bakım turunda
 * (@ref exp_tick) çalışır; uygulama görev sayısı 3'te kalır (FR-01, FR-81).
 *
 * Senaryo parametreleri yalnızca telemetri dururken (IDLE / ARMED / DONE)
 * değiştirilir; koşan bir deneyin ortasında parametre değişmez (FR-85, MR-07).
 *
 * LED'ler: IDLE/ARMED = yeşil, WARMUP = turuncu, MEASURING = mavi,
 * DRAINING/DONE = kırmızı (FR-90).
 */

#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Telemetri görevi — **yüksek öncelik (3)**.
 * @ingroup tasks
 * @param   arg  Kullanılmıyor.
 *
 * Telemetri kapalıyken bir bildirim bekleyerek **süresiz bloklanır** (S0, FR-22).
 * Açıkken her periyotta: ek CPU işi (S4/S5) → 64 baytlık TEL mesajı → `txQ`'ya
 * bloklamadan gönder (FR-26) → `vTaskDelayUntil` ile mutlak periyot (FR-20).
 *
 * @note    FreeRTOS 10.3.1'de `xTaskDelayUntil` henüz yoktur; aynı mutlak-tick
 *          davranışı `vTaskDelayUntil` ile sağlanır (10.4'te yeniden adlandırıldı).
 * @par Paylaşılan durum
 *      Yazar: telemetri istatistikleri (tek yazar), `g_cnt_task.txq_drop_tel / fmt_err_tel`.
 */
void TelemetryTask(void *arg);

/**
 * @brief   Ek CPU işi: sonucu kullanılan, optimize edilemeyen sabit iterasyonlu hesap (FR-23).
 * @ingroup expfsm
 * @param   iters  İterasyon sayısı.
 *
 * Her adım öncekinin sonucuna bağlıdır (doğrusal eşlik üreteci); derleyici döngüyü
 * açamaz, sonuç `volatile` bir değişkene yazıldığı için silemez. `vTaskDelay`
 * kullanılmaz — o CPU'yu bırakır, yük üretmez (FR-24).
 */
void calibrated_work(uint32_t iters);

/**
 * @brief   Durum makinesinin bir turu: komutları işler, zamanlı geçişleri yapar.
 * @ingroup expfsm
 * @note    **Yalnızca ButtonTask** içinden, her döngüde çağrılır (en geç 50 ms'de bir).
 */
void exp_tick(void);

/**
 * @brief   Ölçüm durumunda mı (buton olayları kayda alınır mı).
 * @ingroup expfsm
 * @return  `true`: MEASURING.
 * @note    Buton ISR'ından çağrılır; tek bir `volatile bool` okur.
 */
bool exp_is_measuring(void);

/**
 * @brief   Aktif senaryonun adı ("S0" … "S5").
 * @ingroup expfsm
 * @return  Sabit dizge.
 */
const char *exp_scenario_name(void);

#endif /* EXPERIMENT_H */
