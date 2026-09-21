/**
 * @file    protocol.h
 * @brief   UART protokolü: mesaj biçimlendirme ve komut ayrıştırma.
 * @ingroup proto
 */

/**
 * @defgroup proto UART Protokolü
 * @brief    Kart ↔ PC satır protokolü (tasarım §6).
 *
 * **Ölçüm mesajları** (TEL, BTN) tam 64 bayttır: ASCII içerik, 63 bayta kadar
 * boşluk dolgusu, 64. bayt LF (FR-50, FR-51). Sabit boy, her mesajın hat süresini
 * senaryolar arasında eşit tutar: 64 × 10 bit / 115200 = 5,556 ms.
 *
 * **Kontrol mesajları** (STA, CNT, TST, REC, ACK, ERR, END) değişken uzunluktadır ve
 * yalnızca ölçüm dışında gönderilir (FR-91).
 *
 * **PC → kart komutları** LF ile biten satırlardır: `CMD,SCEN,S3`, `CMD,START[,n]`,
 * `CMD,STOP`, `CMD,DUMP`, `CMD,STAT`.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"

/**
 * @brief   Sabit 64 baytlık bir ölçüm mesajı üretir (TEL / BTN).
 * @ingroup proto
 * @param   m         Doldurulacak mesaj.
 * @param   kind      `MSG_TEL` veya `MSG_BTN`.
 * @param   event_id  `MSG_BTN` için olay kimliği; diğerlerinde 0.
 * @param   fmt       `printf` biçimi; LF **içermemeli**.
 * @return  `false`: içerik 63 baytı aşıyor. Mesaj **kesilmez**, gönderilmemelidir (FR-52).
 * @note    Görev bağlamı (newlib `vsnprintf` kullanır).
 */
bool proto_fmt_fixed(TxMsg *m, MsgKind kind, uint32_t event_id, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * @brief   Değişken uzunlukta, LF ile biten bir kontrol satırı üretir.
 * @ingroup proto
 * @param   m     Doldurulacak mesaj (`kind = MSG_CTRL` veya `MSG_REC`).
 * @param   kind  Mesaj türü.
 * @param   fmt   `printf` biçimi; LF **içermemeli**, sona eklenir.
 * @return  `false`: satır tampona sığmadı.
 * @note    Görev bağlamı.
 */
bool proto_fmt_line(TxMsg *m, MsgKind kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** @brief Komut türü. @ingroup proto */
typedef enum {
    CMD_INVALID = 0,   /**< Tanınmadı. */
    CMD_SCEN,          /**< `CMD,SCEN,Sx` — senaryo seç. */
    CMD_START,         /**< `CMD,START[,n]` — deneyi başlat; n = hedef olay sayısı. */
    CMD_STOP,          /**< `CMD,STOP` — iptal, IDLE'a dön. */
    CMD_DUMP,          /**< `CMD,DUMP` — kayıtları dök. */
    CMD_STAT           /**< `CMD,STAT` — durum ve sayaçlar. */
} CmdType;

/** @brief Ayrıştırılmış komut. @ingroup proto */
typedef struct {
    CmdType  type;       /**< Komut türü. */
    uint8_t  scenario;   /**< `CMD_SCEN` için 0…5. */
    uint32_t count;      /**< `CMD_START` için hedef olay sayısı; 0 = varsayılan. */
} Command;

/**
 * @brief   Bir komut satırını ayrıştırır.
 * @ingroup proto
 * @param   line  LF'siz, NUL ile biten satır.
 * @param   out   Sonuç.
 * @return  `true`: geçerli komut. `false`: `out->type = CMD_INVALID`.
 */
bool proto_parse_cmd(const char *line, Command *out);

#endif /* PROTOCOL_H */
