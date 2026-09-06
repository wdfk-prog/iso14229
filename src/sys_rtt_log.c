#include "log.h"

/* log.c excludes RTT so that only this backend defines the shared log functions. */
#if UDS_SYS == UDS_SYS_RTT

#include <stdarg.h>

#if UDS_LOG_LEVEL > UDS_LOG_NONE

#ifdef UDS_RTTHREAD_ULOG_ENABLED
#define DBG_TAG "UDS.core"
#if UDS_LOG_LEVEL >= UDS_LOG_DEBUG
#define DBG_LVL LOG_LVL_DBG
#elif UDS_LOG_LEVEL == UDS_LOG_INFO
#define DBG_LVL LOG_LVL_INFO
#elif UDS_LOG_LEVEL == UDS_LOG_WARN
#define DBG_LVL LOG_LVL_WARNING
#elif UDS_LOG_LEVEL == UDS_LOG_ERROR
#define DBG_LVL LOG_LVL_ERROR
#else
#define DBG_LVL LOG_LVL_ASSERT
#endif // UDS_LOG_LEVEL >= UDS_LOG_DEBUG
#include <rtdbg.h>

/* Preserve each message's severity; the configured verbosity is only a filter. */
static rt_uint32_t rtt_log_level(UDS_LogLevel_t level) {
    switch (level) {
    case UDS_LOG_NONE: return LOG_LVL_ASSERT;
    case UDS_LOG_ERROR: return LOG_LVL_ERROR;
    case UDS_LOG_WARN: return LOG_LVL_WARNING;
    case UDS_LOG_INFO: return LOG_LVL_INFO;
    default: return LOG_LVL_DBG;
    }
}
#endif // UDS_RTTHREAD_ULOG_ENABLED

void UDS_LogWrite(UDS_LogLevel_t level, const char *tag, const char *format, ...) {
    va_list list;
    (void)level;
    (void)tag;
    va_start(list, format);
#ifdef UDS_RTTHREAD_ULOG_ENABLED
    ulog_voutput(rtt_log_level(level), DBG_TAG, RT_TRUE, RT_NULL, 0, 0, 0, format, list);
#else
    /* The bounded fallback buffer may truncate a message before console output. */
    char log_buf[UDS_RTTHREAD_LOG_BUFFER_SIZE];
    rt_vsnprintf(log_buf, sizeof(log_buf), format, list);
    rt_kprintf("%s", log_buf);
#endif // UDS_RTTHREAD_ULOG_ENABLED
    va_end(list);
}

void UDS_LogSDUInternal(UDS_LogLevel_t level, const char *tag, const uint8_t *buffer,
                        size_t buff_len, const UDSSDU_t *info) {
    (void)info;
#ifdef UDS_RTTHREAD_ULOG_ENABLED
    (void)level;
    ulog_hexdump(tag, 16, (rt_uint8_t *)buffer, buff_len);
#else
    for (size_t i = 0; i < buff_len; i++) {
        UDS_LogWrite(level, tag, "%02x ", buffer[i]);
    }
    UDS_LogWrite(level, tag, "\n");
#endif // UDS_RTTHREAD_ULOG_ENABLED
}

#ifdef UDS_RTTHREAD_ULOG_ENABLED
#undef DBG_TAG
#undef DBG_LVL
#endif // UDS_RTTHREAD_ULOG_ENABLED

#endif // UDS_LOG_LEVEL > UDS_LOG_NONE
#endif // UDS_SYS == UDS_SYS_RTT
