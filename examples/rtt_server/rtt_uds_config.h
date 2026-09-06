/**
 * @file rtt_uds_config.h
 * @brief Default configurations for the UDS library on RT-Thread.
 * @details This file provides default configurations for the UDS (Unified Diagnostic Services)
 *          library tailored for the RT-Thread operating system. These values can be
 *          overridden by defining them in the project's `rtconfig.h` or through the
 *          Kconfig system. This configuration is for the UDS library available at
 *          https://github.com/driftregion/iso14229.
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2025-11-17
 *
 * @copyright Copyright (c) 2025
 *
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2025-11-17 1.0     wdfk-prog   first version
 */
#ifndef __RTT_UDS_CONFIG_H__
#define __RTT_UDS_CONFIG_H__

#include <rtthread.h>
#include <rtdevice.h>

/**
 * @brief Specifies the system type for the UDS library.
 * @details Selects the symbolic UDS_SYS_RTT value when RT_THREAD_PRIORITY_MAX
 *          is detected and no host system was selected. UDS_SYS_RTT is defined
 *          by the library; its value remains distinct from UDS_SYS_ZEPHYR.
 */
#ifndef UDS_SYS
#ifdef RT_THREAD_PRIORITY_MAX
#define UDS_SYS UDS_SYS_RTT
#endif /* RT_THREAD_PRIORITY_MAX */
#endif /* !defined(UDS_SYS) */

/* ------------------- Logging Configuration ------------------- */

/**
 * @def UDS_LOG_LEVEL
 * @brief Defines the log level for the UDS library.
 * @details Levels range from 0 (None) to 5 (Verbose); errors start at 1.
 *          The default level is 3 (Info).
 */
#ifndef UDS_LOG_LEVEL
#define UDS_LOG_LEVEL 3
#endif

/**
 * @def UDS_RTTHREAD_ULOG_ENABLED
 * @brief Enables the use of RT-Thread's ULOG component.
 * @details Enable this option through Kconfig or define it in rtconfig.h.
 *          RT_USING_ULOG must also be enabled. Leaving this option undefined
 *          selects the rt_kprintf fallback even when ULOG is available.
 */
#if defined(UDS_RTTHREAD_ULOG_ENABLED) && !defined(RT_USING_ULOG)
#error "UDS_RTTHREAD_ULOG_ENABLED requires RT_USING_ULOG"
#endif /* defined(UDS_RTTHREAD_ULOG_ENABLED) && !defined(RT_USING_ULOG) */

#ifndef UDS_RTTHREAD_ULOG_ENABLED
/**
 * @def UDS_RTTHREAD_LOG_BUFFER_SIZE
 * @brief Log buffer size when not using ULOG.
 * @details This is only needed when ULOG is not used. It defines the size of the
 *          character buffer for formatting log messages.
 */
#ifndef UDS_RTTHREAD_LOG_BUFFER_SIZE
#define UDS_RTTHREAD_LOG_BUFFER_SIZE 256
#endif

/**
 * @def UDS_CONFIG_LOG_COLORS
 * @brief Enables or disables colored log output.
 * @details For the console fallback, Kconfig or rtconfig.h may set this to 1
 *          to enable ANSI color codes. An undefined option defaults to 0 so
 *          disabling the Kconfig option keeps color output disabled.
 */
#ifndef UDS_CONFIG_LOG_COLORS
#define UDS_CONFIG_LOG_COLORS 0
#endif /* !defined(UDS_CONFIG_LOG_COLORS) */
#endif /* !defined(UDS_RTTHREAD_ULOG_ENABLED) */

/**
 * @brief Size of the event dispatch table.
 * @details Corresponds to UDS_EVT_MAX + 1 to allow O(1) array indexing
 *          based on the UDSEvent_t enum value.
 */
#ifndef UDS_RTT_EVENT_TABLE_SIZE
#define UDS_RTT_EVENT_TABLE_SIZE  (UDS_EVT_MAX + 1)
#endif

#endif /* __RTT_UDS_CONFIG_H__ */