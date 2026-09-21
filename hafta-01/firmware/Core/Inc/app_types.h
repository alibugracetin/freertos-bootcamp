/**
 * @file    app_types.h
 * @brief   Görevler ve kesmeler arasında kuyruklarla taşınan veri tipleri.
 * @ingroup tasks
 *
 * Kuyruklar veriyi **değerle kopyalar** (FR-35): gönderen tarafın yerel
 * değişkeninin ömrü, alıcıyı etkilemez.
 */

#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include "app_config.h"

/**
 * @brief   Buton ISR'ından `ButtonTask`'a giden olay (`buttonQ` elemanı).
 * @ingroup tasks
 */
typedef struct {
    uint32_t event_id;   /**< Kabul edilen basışın benzersiz kimliği (FR-14). */
    uint32_t t0_us;      /**< ISR girişinde alınan t₀ [µs] (FR-11). */
} ButtonEvent;

/**
 * @brief   TX kuyruğundaki mesajın türü.
 * @ingroup tasks
 */
typedef enum {
    MSG_TEL = 0,   /**< Telemetri, tam 64 bayt. */
    MSG_BTN,       /**< Buton yanıtı, tam 64 bayt; t₃/t₄ bu olayın kaydına yazılır. */
    MSG_REC,       /**< Deney sonu kayıt dökümü satırı, değişken uzunluk. */
    MSG_CTRL       /**< Komut yanıtı / durum / sayaç satırı, değişken uzunluk. */
} MsgKind;

/**
 * @brief   `txQ` elemanı: UART'tan gönderilecek bir mesaj.
 * @ingroup tasks
 */
typedef struct {
    MsgKind  kind;                    /**< Mesaj türü. */
    uint32_t event_id;                /**< `MSG_BTN` için kapatılacak kaydın kimliği; diğerlerinde 0. */
    uint16_t len;                     /**< Gönderilecek bayt sayısı. */
    char     data[APP_TX_BUF_LEN];    /**< ASCII içerik, LF ile biter. */
} TxMsg;

#endif /* APP_TYPES_H */
