/**
 * @file    protocol.c
 * @brief   UART protokolünün gerçekleştirimi.
 * @ingroup proto
 */

#include "protocol.h"
#include "app_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @copydoc proto_fmt_fixed */
bool proto_fmt_fixed(TxMsg *m, MsgKind kind, uint32_t event_id, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(m->data, APP_MSG_LEN, fmt, ap);   /* en fazla 63 + NUL */
    va_end(ap);

    if (n < 0 || n > (int)(APP_MSG_LEN - 1u)) {
        return false;                                         /* FR-52: sessiz kesme yok */
    }
    memset(&m->data[n], ' ', (APP_MSG_LEN - 1u) - (size_t)n); /* FR-51: boşluk dolgusu */
    m->data[APP_MSG_LEN - 1u] = '\n';
    m->len      = APP_MSG_LEN;
    m->kind     = kind;
    m->event_id = event_id;
    return true;
}

/** @copydoc proto_fmt_line */
bool proto_fmt_line(TxMsg *m, MsgKind kind, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(m->data, APP_TX_BUF_LEN - 1u, fmt, ap);
    va_end(ap);

    if (n < 0 || n > (int)(APP_TX_BUF_LEN - 2u)) {
        return false;
    }
    m->data[n]  = '\n';
    m->len      = (uint16_t)(n + 1);
    m->kind     = kind;
    m->event_id = 0u;
    return true;
}

bool proto_parse_cmd(const char *line, Command *out)
{
    out->type     = CMD_INVALID;
    out->scenario = 0u;
    out->count    = 0u;

    if (strncmp(line, "CMD,", 4) != 0) {
        return false;
    }
    const char *arg = line + 4;

    if (strcmp(arg, "STOP") == 0) { out->type = CMD_STOP; return true; }
    if (strcmp(arg, "DUMP") == 0) { out->type = CMD_DUMP; return true; }
    if (strcmp(arg, "STAT") == 0) { out->type = CMD_STAT; return true; }

    if (strncmp(arg, "SCEN,S", 6) == 0 && arg[6] >= '0' && arg[6] <= '5' && arg[7] == '\0') {
        out->type     = CMD_SCEN;
        out->scenario = (uint8_t)(arg[6] - '0');
        return true;
    }

    if (strncmp(arg, "START", 5) == 0) {
        if (arg[5] == '\0') {
            out->type = CMD_START;
            return true;
        }
        if (arg[5] == ',') {
            char *end = NULL;
            const unsigned long n = strtoul(&arg[6], &end, 10);
            if (end != &arg[6] && *end == '\0' && n >= 1u && n <= APP_REC_POOL_SIZE) {
                out->type  = CMD_START;
                out->count = (uint32_t)n;
                return true;
            }
        }
    }
    return false;
}
