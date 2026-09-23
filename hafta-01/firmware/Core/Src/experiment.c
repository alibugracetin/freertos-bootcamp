/**
 * @file    experiment.c
 * @brief   Senaryolar, deney durum makinesi ve TelemetryTask (T-13).
 * @ingroup expfsm
 */

#include "experiment.h"

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_config.h"
#include "app_diag.h"
#include "app_tasks.h"
#include "app_types.h"
#include "protocol.h"
#include "record.h"
#include "timing.h"
#include "uart_link.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- senaryolar */

/** @brief Bir senaryonun sabit parametreleri. */
typedef struct {
    const char *name;        /**< "S0" … "S5". */
    uint16_t    period_ms;   /**< Telemetri periyodu; 0 = telemetri kapalı. */
    uint32_t    work_iters;  /**< Periyot başına ek CPU işi [iterasyon]. */
} Scenario;

/** @brief Ödevin altı zorunlu senaryosu (MR, §6.1). */
static const Scenario k_scen[] = {
    { "S0",   0u, 0u                 },   /* referans: telemetri kapalı */
    { "S1", 100u, 0u                 },   /* 10 Hz  */
    { "S2",  20u, 0u                 },   /* 50 Hz  */
    { "S3",  10u, 0u                 },   /* 100 Hz */
    { "S4",  10u, APP_WORK_ITERS_S4  },   /* 100 Hz + ≈2 ms iş */
    { "S5",  10u, APP_WORK_ITERS_S5  },   /* 100 Hz + ≈5 ms iş */
};
#define SCEN_COUNT  (sizeof k_scen / sizeof k_scen[0])

/** @brief Deney durumları. */
typedef enum { EXP_IDLE = 0, EXP_ARMED, EXP_WARMUP, EXP_MEASURING, EXP_DRAINING, EXP_DONE } ExpState;

static const char *const k_state_name[] = { "IDLE", "ARMED", "WARMUP", "MEASURING", "DRAINING", "DONE" };

/* ------------------------------------------------------ paylaşılan durum */

/**
 * @brief   Durum makinesinin iç durumu.
 * @par Paylaşılan durum
 *      Yalnızca ButtonTask (@ref exp_tick) yazar ve okur; aşağıdaki iki bayrak hariç.
 */
static ExpState   s_state = EXP_IDLE;
static uint8_t    s_scen;
static uint32_t   s_target = APP_DEFAULT_TARGET;
static TickType_t s_state_since;
static bool       s_announced;

/** @brief Buton ISR'ının okuduğu ölçüm bayrağı. Yazar: ButtonTask. */
static volatile bool s_measuring;
/** @brief TelemetryTask'ın döngü koşulu. Yazar: ButtonTask. */
static volatile bool s_tel_run;
/** @brief TelemetryTask periyodik döngüde mi. Yazar: TelemetryTask. */
static volatile bool s_tel_active;
static TaskHandle_t  s_tel_task;

/**
 * @brief   `txQ` yüksek su seviyesinin **ölçüm fazı sonundaki** değeri (FR-65).
 *
 * Canlı sayaç `g_cnt_task.txq_hwm` döküm sırasında da artar: 30+ REC satırı aynı
 * kuyruktan geçer ve sayacı kuyruk kapasitesine kadar şişirir (S2 resmî koşusunda
 * 16/16 görüldü). Rapora giren değer ölçüm fazını anlatmalıdır, bu yüzden
 * DRAINING'e geçerken anlık görüntü alınır ve `CNT` satırında bu bildirilir.
 */
static uint32_t s_txq_hwm_meas;

/**
 * @brief   Gerçekleşen telemetri istatistikleri (AR-11).
 * @par Paylaşılan durum
 *      Yazar: TelemetryTask (koşarken). Okur: ButtonTask, yalnızca telemetri dururken.
 *      Sıfırlama: senaryo seçiminde, telemetri dururken.
 */
static struct {
    uint32_t sent;                       /**< Üretilen TEL sayısı. */
    uint32_t per_n, per_sum, per_min, per_max;   /**< Ardışık üretim başlangıçları arası [µs]. */
    uint32_t work_n, work_sum, work_max;         /**< calibrated_work duvar saati [µs]. */
} s_tel;

/** @brief `calibrated_work` sonucunun yazıldığı yer; derleyicinin döngüyü silmesini engeller. */
static volatile uint32_t s_work_sink;

/* ------------------------------------------------------------------ yardımcılar */

void calibrated_work(uint32_t iters)
{
    uint32_t acc = 0x12345678u;
    for (uint32_t i = 0; i < iters; i++) {
        acc = acc * 1664525u + 1013904223u;
    }
    s_work_sink = acc;
}

bool exp_is_measuring(void)
{
    return s_measuring;
}

const char *exp_scenario_name(void)
{
    return k_scen[s_scen].name;
}

/** @brief Bir kontrol satırını `txQ`'ya koyar (yalnızca ölçüm dışında çağrılır). */
static void send_ctrl(const TxMsg *m)
{
    (void)xQueueSend(g_tx_q, m, pdMS_TO_TICKS(500));
}

#define SEND_LINE(...)                                                   \
    do {                                                                 \
        TxMsg m_;                                                        \
        if (proto_fmt_line(&m_, MSG_CTRL, __VA_ARGS__)) { send_ctrl(&m_); } \
    } while (0)

static void set_leds(ExpState st)
{
    HAL_GPIO_WritePin(GPIOD, LED_GREEN_Pin | LED_ORANGE_Pin | LED_RED_Pin | LED_BLUE_Pin, GPIO_PIN_RESET);
    uint16_t pin = LED_GREEN_Pin;
    switch (st) {
    case EXP_WARMUP:    pin = LED_ORANGE_Pin; break;
    case EXP_MEASURING: pin = LED_BLUE_Pin;   break;
    case EXP_DRAINING:
    case EXP_DONE:      pin = LED_RED_Pin;    break;
    default:            break;
    }
    HAL_GPIO_WritePin(GPIOD, pin, GPIO_PIN_SET);
}

static void send_status(void)
{
    SEND_LINE("STA,%s,%s,%lu,%lu,%u", k_state_name[s_state], k_scen[s_scen].name,
              (unsigned long)g_cnt_isr.events, (unsigned long)s_target,
              (unsigned)k_scen[s_scen].period_ms);
}

static void enter(ExpState st)
{
    s_state       = st;
    s_state_since = xTaskGetTickCount();
    set_leds(st);
}

/** @brief Telemetriyi durdurur; görev bir sonraki periyotta döngüden çıkar. */
static void telemetry_stop(void)
{
    s_tel_run = false;
}

/** @brief Aktif senaryonun telemetrisini başlatır (S0'da hiçbir şey yapmaz). */
static void telemetry_start(void)
{
    if (k_scen[s_scen].period_ms == 0u || s_tel_task == NULL) {
        return;                                   /* S0: görev bloklu kalır (FR-22) */
    }
    s_tel_run = true;
    xTaskNotifyGive(s_tel_task);
}

/** @brief Senaryo değişiminde tüm sayaçları, kayıtları ve istatistikleri sıfırlar (FR-86). */
static void reset_measurement(void)
{
    rec_reset();
    memset((void *)&s_tel, 0, sizeof s_tel);
    s_tel.per_min = UINT32_MAX;

    /* ISR sayaçları ölçüm dışında da artabilir (idle_press, rx_*); sıfırlama anında
     * yarışmasınlar diye kısa kritik bölüm. Ölçüm yolunda değiliz. */
    taskENTER_CRITICAL();
    memset((void *)&g_cnt_isr, 0, sizeof g_cnt_isr);
    taskEXIT_CRITICAL();
    memset((void *)&g_cnt_task, 0, sizeof g_cnt_task);
    s_txq_hwm_meas = 0u;
    g_diag.max_t1_minus_t0  = 0u;
    g_diag.max_dma_to_tc_us = 0u;
}

/* ------------------------------------------------------------------- döküm */

static const char *status_name(uint8_t st)
{
    switch (st) {
    case REC_OPEN:      return "open";
    case REC_OK:        return "ok";
    case REC_BTNQ_DROP: return "btnq_drop";
    case REC_TXQ_DROP:  return "tx_drop";
    case REC_TX_ERROR:  return "tx_error";
    case REC_TIMEOUT:   return "timeout";
    default:            return "free";
    }
}

/**
 * @brief   Kayıtları, telemetri istatistiğini ve sayaçları döker (FR-66).
 *
 * Satırlar: `REC,<scen>,<id>,<t0>,<t1>,<t2>,<t3>,<t4>,<status>` (alınmamış damga
 * **boş**, MR-21), `TST,...`, `CNI,...`, `CNT,...`, `END,<scen>,<kayıt sayısı>`.
 */
static void dump_records(void)
{
    const char *sn = k_scen[s_scen].name;
    uint32_t n = 0u;

    for (uint32_t s = 0; s < APP_REC_POOL_SIZE; s++) {
        EventRecord r;
        if (!rec_read(s, &r) || r.status == REC_FREE) {
            continue;
        }
        char f[TS_COUNT][11];
        for (unsigned i = 0; i < TS_COUNT; i++) {
            if (r.have & (1u << i)) {
                (void)snprintf(f[i], sizeof f[i], "%lu", (unsigned long)r.t[i]);
            } else {
                f[i][0] = '\0';
            }
        }
        TxMsg m;
        if (proto_fmt_line(&m, MSG_REC, "REC,%s,%lu,%s,%s,%s,%s,%s,%s", sn, (unsigned long)r.event_id,
                           f[0], f[1], f[2], f[3], f[4], status_name(r.status))) {
            send_ctrl(&m);
            n++;
        }
    }

    const uint32_t per_avg  = s_tel.per_n  ? s_tel.per_sum  / s_tel.per_n  : 0u;
    const uint32_t work_avg = s_tel.work_n ? s_tel.work_sum / s_tel.work_n : 0u;
    SEND_LINE("TST,%s,%lu,%lu,%lu,%lu,%lu,%lu", sn, (unsigned long)s_tel.sent,
              (unsigned long)(s_tel.per_n ? s_tel.per_min : 0u), (unsigned long)per_avg,
              (unsigned long)s_tel.per_max, (unsigned long)work_avg, (unsigned long)s_tel.work_max);

    const volatile IsrCounters *ci = &g_cnt_isr;
    SEND_LINE("CNI,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
              (unsigned long)ci->accepted, (unsigned long)ci->debounce_rej, (unsigned long)ci->btnq_drop,
              (unsigned long)ci->rec_ovf, (unsigned long)ci->rec_mismatch, (unsigned long)ci->unarmed_rej,
              (unsigned long)ci->events, (unsigned long)ci->idle_press, (unsigned long)ci->rx_overflow,
              (unsigned long)ci->rx_err, (unsigned long)ci->cmd_busy);

    const volatile TaskCounters *ct = &g_cnt_task;
    SEND_LINE("CNT,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
              (unsigned long)ct->txq_drop_tel, (unsigned long)ct->txq_drop_btn, (unsigned long)ct->rec_mismatch,
              (unsigned long)ct->uart_err, (unsigned long)ct->uart_timeout, (unsigned long)ct->rec_mismatch_tx,
              (unsigned long)ct->fmt_err_tel, (unsigned long)ct->fmt_err_btn, (unsigned long)s_txq_hwm_meas,
              (unsigned long)ct->cmd_err);

    SEND_LINE("END,%s,%lu", sn, (unsigned long)n);
}

/* ------------------------------------------------------------------ komutlar */

static void reject(const char *why, const char *line)
{
    g_cnt_task.cmd_err++;
    SEND_LINE("ERR,%s,%s", why, line);
}

static void handle_command(const char *line)
{
    Command c;

    if (!proto_parse_cmd(line, &c)) {
        reject("unknown", line);
        return;
    }

    switch (c.type) {
    case CMD_SCEN:
        if (s_state != EXP_IDLE && s_state != EXP_ARMED && s_state != EXP_DONE) {
            reject("state", line);                /* FR-85 */
            return;
        }
        if (s_tel_active) {
            /* STOP'tan sonra telemetri en fazla bir periyot (≤100 ms) daha döner;
             * o bitmeden istatistikler sıfırlanırsa yarışırdı. */
            reject("busy", line);
            return;
        }
        s_scen = c.scenario;
        reset_measurement();
        enter(EXP_ARMED);
        SEND_LINE("ACK,SCEN,%s,%u,%lu", k_scen[s_scen].name, (unsigned)k_scen[s_scen].period_ms,
                  (unsigned long)k_scen[s_scen].work_iters);
        send_status();
        break;

    case CMD_START:
        if (s_state != EXP_ARMED) {
            reject("state", line);
            return;
        }
        s_target = c.count ? c.count : APP_DEFAULT_TARGET;
        enter(EXP_WARMUP);
        SEND_LINE("ACK,START,%lu", (unsigned long)s_target);
        send_status();
        telemetry_start();                        /* ısınmada telemetri koşar (FR-87) */
        break;

    case CMD_STOP:
        s_measuring = false;
        telemetry_stop();
        enter(EXP_IDLE);
        SEND_LINE("ACK,STOP");
        send_status();
        break;

    case CMD_DUMP:
        if (s_state != EXP_DONE) {
            reject("state", line);
            return;
        }
        dump_records();
        break;

    case CMD_STAT:
        if (s_state == EXP_MEASURING) {
            reject("state", line);                /* FR-91: ölçümde ek trafik yok */
            return;
        }
        send_status();
        break;

    default:
        reject("unknown", line);
        break;
    }
}

/* ---------------------------------------------------------------- durum makinesi */

void exp_tick(void)
{
    char line[32];

    if (!s_announced) {                           /* açılışta bir kez */
        s_announced = true;
        s_tel.per_min = UINT32_MAX;
        enter(EXP_IDLE);
        SEND_LINE("BOOT,hafta01,work_us_per_100k=%lu", (unsigned long)g_diag.work_us_per_100k);
        send_status();
    }

    if (uart_link_take_command(line, sizeof line)) {
        handle_command(line);
    }

    const TickType_t elapsed = xTaskGetTickCount() - s_state_since;

    switch (s_state) {
    case EXP_WARMUP:
        if (elapsed >= pdMS_TO_TICKS(APP_WARMUP_MS)) {
            enter(EXP_MEASURING);
            send_status();                        /* ölçüm başlamadan önceki son kontrol mesajı */
            s_measuring = true;
        }
        break;

    case EXP_MEASURING:
        if (g_cnt_isr.events >= s_target) {
            s_measuring = false;                  /* yeni olay açılmaz */
            s_txq_hwm_meas = g_cnt_task.txq_hwm;  /* dökümden önceki değer */
            telemetry_stop();
            enter(EXP_DRAINING);
        }
        break;

    case EXP_DRAINING: {
        const bool drained = !s_tel_active && uxQueueMessagesWaiting(g_tx_q) == 0u &&
                             !uart_link_busy() && rec_count_open() == 0u;
        if (drained || elapsed >= pdMS_TO_TICKS(APP_DRAIN_TIMEOUT_MS)) {
            (void)rec_timeout_open();             /* MR-06 */
            enter(EXP_DONE);
            send_status();
        }
        break;
    }

    default:
        break;
    }
}

/* ---------------------------------------------------------------- TelemetryTask */

void TelemetryTask(void *arg)
{
    (void)arg;
    s_tel_task = xTaskGetCurrentTaskHandle();
    uint32_t seq = 0u;

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* S0 ve IDLE: süresiz bloklu */

        const Scenario *sc   = &k_scen[s_scen];           /* koşarken değişmez (FR-85) */
        const TickType_t per = pdMS_TO_TICKS(sc->period_ms);
        TickType_t last      = xTaskGetTickCount();
        uint32_t   prev_us   = 0u;
        bool       have_prev = false;

        s_tel_active = true;
        while (s_tel_run) {
            const uint32_t t_start = timer_us();

            if (have_prev) {                              /* gerçekleşen periyot (AR-11) */
                const uint32_t p = t_start - prev_us;
                s_tel.per_n++;
                s_tel.per_sum += p;
                if (p < s_tel.per_min) { s_tel.per_min = p; }
                if (p > s_tel.per_max) { s_tel.per_max = p; }
            }
            prev_us   = t_start;
            have_prev = true;

            if (sc->work_iters != 0u) {                   /* S4 / S5 ek CPU yükü */
                calibrated_work(sc->work_iters);
                const uint32_t w = timer_us() - t_start;
                s_tel.work_n++;
                s_tel.work_sum += w;
                if (w > s_tel.work_max) { s_tel.work_max = w; }
            }

            TxMsg m;
            if (!proto_fmt_fixed(&m, MSG_TEL, 0u, "TEL,%lu,%s,%lu",
                                 (unsigned long)++seq, sc->name, (unsigned long)t_start)) {
                g_cnt_task.fmt_err_tel++;
            } else if (xQueueSend(g_tx_q, &m, 0) != pdPASS) {   /* FR-26: bloklamaz */
                g_cnt_task.txq_drop_tel++;
            }
            s_tel.sent++;

            vTaskDelayUntil(&last, per);                  /* FR-20: mutlak periyot */
        }
        s_tel_active = false;
    }
}
