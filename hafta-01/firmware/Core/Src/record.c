/**
 * @file    record.c
 * @brief   Ölçüm kayıt defterinin gerçekleştirimi.
 * @ingroup record
 */

#include "record.h"
#include "app_config.h"

#include <stddef.h>

/**
 * @brief   Kayıt havuzu.
 *
 * @par Paylaşılan durum
 *      Yazarlar: buton ISR'ı (açılış, t₀), `ButtonTask` (t₁, t₂), `UartTxTask` (t₃),
 *      UART TC ISR'ı (t₄, kapanış). Aynı slota yazan bağlamlar kesin sırayla çalışır
 *      (bkz. @ref record). `volatile`: derleyicinin farklı bağlamlarda yapılan
 *      yazmaları önbelleğe alıp birleştirmesini engeller.
 */
static volatile EventRecord s_pool[APP_REC_POOL_SIZE];

/**
 * @brief   Kimliğin düştüğü slot.
 * @param   id  Olay kimliği.
 * @return  Slot işaretçisi (`id % APP_REC_POOL_SIZE`).
 */
static inline volatile EventRecord *slot_of(uint32_t id)
{
    return &s_pool[id % APP_REC_POOL_SIZE];
}

bool rec_open(uint32_t id, uint32_t t0_us)
{
    volatile EventRecord *r = slot_of(id);

    if (id == 0u || r->status != REC_FREE) {
        return false;                      /* FR-61: eski kayıt ezilmez */
    }
    r->event_id = id;
    r->t[TS_T0] = t0_us;
    r->have     = (uint8_t)(1u << TS_T0);
    r->status   = REC_OPEN;                /* en son: kayıt artık "canlı" */
    return true;
}

bool rec_stamp(uint32_t id, TsIndex idx, uint32_t t_us)
{
    volatile EventRecord *r = slot_of(id);

    if (idx >= TS_COUNT || r->event_id != id || r->status != REC_OPEN) {
        return false;                      /* FR-46: yanlış kayda yazma yok */
    }
    r->t[idx] = t_us;
    r->have  |= (uint8_t)(1u << idx);
    return true;
}

bool rec_close(uint32_t id, RecStatus status)
{
    volatile EventRecord *r = slot_of(id);

    if (r->event_id != id || r->status != REC_OPEN || status <= REC_OPEN) {
        return false;
    }
    r->status = (uint8_t)status;
    return true;
}

bool rec_read(uint32_t slot, EventRecord *out)
{
    if (slot >= APP_REC_POOL_SIZE || out == NULL) {
        return false;
    }
    volatile const EventRecord *r = &s_pool[slot];
    out->event_id = r->event_id;
    for (unsigned i = 0; i < TS_COUNT; i++) {
        out->t[i] = r->t[i];
    }
    out->have   = r->have;
    out->status = r->status;
    return true;
}

uint32_t rec_count_open(void)
{
    uint32_t n = 0u;
    for (unsigned s = 0; s < APP_REC_POOL_SIZE; s++) {
        if (s_pool[s].status == REC_OPEN) {
            n++;
        }
    }
    return n;
}

uint32_t rec_timeout_open(void)
{
    uint32_t n = 0u;
    for (unsigned s = 0; s < APP_REC_POOL_SIZE; s++) {
        if (s_pool[s].status == REC_OPEN && rec_close(s_pool[s].event_id, REC_TIMEOUT)) {
            n++;
        }
    }
    return n;
}

void rec_reset(void)
{
    for (unsigned s = 0; s < APP_REC_POOL_SIZE; s++) {
        s_pool[s].status   = REC_FREE;
        s_pool[s].event_id = 0u;
        s_pool[s].have     = 0u;
        for (unsigned i = 0; i < TS_COUNT; i++) {
            s_pool[s].t[i] = 0u;
        }
    }
}

bool rec_selfcheck(uint32_t *opened, uint32_t *rejected)
{
    bool ok = true;
    uint32_t n_open = 0u, n_rej = 0u;

    rec_reset();

    /* 1) 100 sahte olay: yalnızca havuz boyutu kadarı açılabilmeli. */
    for (uint32_t id = 1u; id <= 100u; id++) {
        if (rec_open(id, 1000u * id)) { n_open++; } else { n_rej++; }
    }
    ok &= (n_open == APP_REC_POOL_SIZE) && (n_rej == 100u - APP_REC_POOL_SIZE);

    /* 2) Damga yalnızca slotun sahibine yazılabilir.
     *    id 65 ile id 1 aynı slota düşer; slotun sahibi 1'dir. */
    ok &=  rec_stamp(1u, TS_T1, 1111u);
    ok &= !rec_stamp(1u + APP_REC_POOL_SIZE, TS_T1, 9999u);
    ok &= !rec_stamp(1u, TS_COUNT, 0u);                     /* geçersiz indeks */

    /* 3) Kapatma bir kez yapılabilir; kapalı kayda damga yazılamaz. */
    ok &=  rec_close(1u, REC_OK);
    ok &= !rec_close(1u, REC_OK);
    ok &= !rec_stamp(1u, TS_T2, 2222u);
    ok &= !rec_close(2u, REC_OPEN);                         /* OPEN bir kapanış durumu değil */

    /* 4) Kapanmış ama dökülmemiş kaydın üzerine yeni olay açılamaz. */
    ok &= !rec_open(1u + APP_REC_POOL_SIZE, 5u);

    /* 5) İçerik: t0, t1 ve have bitleri doğru, t2 yazılmamış. */
    EventRecord e;
    ok &= rec_read(1u, &e);
    ok &= (e.event_id == 1u) && (e.t[TS_T0] == 1000u) && (e.t[TS_T1] == 1111u);
    ok &= (e.have == ((1u << TS_T0) | (1u << TS_T1))) && (e.status == REC_OK);

    /* 6) Sıfırlama sonrası tüm slotlar boş ve yeniden açılabilir. */
    rec_reset();
    for (uint32_t s = 0; s < APP_REC_POOL_SIZE; s++) {
        ok &= rec_read(s, &e) && (e.status == REC_FREE);
    }
    ok &= rec_open(1u + APP_REC_POOL_SIZE, 5u);
    rec_reset();

    if (opened)   { *opened   = n_open; }
    if (rejected) { *rejected = n_rej;  }
    return ok;
}
