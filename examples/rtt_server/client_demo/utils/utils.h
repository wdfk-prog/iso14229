/**
 * @file utils.h
 * @brief Utility function prototypes and Tagged Logging System.
 * @details This header provides common system abstractions (time, delay, CRC)
 *          and a standardized logging macro set designed for "Raw Mode" terminal
 *          compatibility (handling explicit carriage returns \r\n).
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2025-12-02
 * 
 * @copyright Copyright (c) 2025  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2025-12-02 1.0     wdfk-prog   first version
 */
#ifndef UTILS_H
#define UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ==========================================================================
 * Time & Data Integrity Helpers
 * ========================================================================== */

/**
 * @brief Retrieves the current system time in milliseconds.
 * @details This function is typically used for timeouts and interval measurements.
 *          It wraps the platform-specific time retrieval mechanism.
 *
 * @return uint32_t Current timestamp in milliseconds.
 */
uint32_t sys_tick_get_ms(void);

/**
 * @brief Blocking delay function.
 * @details Suspends execution of the current thread/task for the specified duration.
 *
 * @param ms Duration to sleep in milliseconds.
 */
void sys_delay_ms(uint32_t ms);

/**
 * @brief Calculates standard CRC32 (ISO 3309).
 * @details Computes the Cyclic Redundancy Check using the standard polynomial
 *          0x04C11DB7. Supports chaining by passing the previous result as `crc`.
 *
 * @param crc  Initial value. Pass 0 for the start of a block, or the result
 *             of a previous call for multi-part data.
 * @param data Pointer to the input data buffer.
 * @param len  Length of the data in bytes.
 * @return uint32_t The calculated CRC32 result.
 */
uint32_t crc32_calc(uint32_t crc, const uint8_t *data, size_t len);

/**
 * @brief Renders a text-based progress bar to stdout.
 * @details Displays a progress bar in the format: `[Label] [=====>    ] 50% (500/1000)`.
 *          It uses the Carriage Return (`\r`) character to overwrite the current line,
 *          creating an animation effect suitable for CLI file transfers.
 *
 * @param current Current progress value (e.g., bytes transferred).
 * @param total   Total expected value (e.g., file size).
 * @param label   Short description string displayed before the bar.
 */
void utils_render_progress(size_t current, size_t total, const char *label);

/* ==========================================================================
 * Tagged Logging System
 * ========================================================================== */

/**
 * @brief Default Log Tag.
 * @details If a source file does not define `LOG_TAG` before including this header,
 *          "App" will be used as the default identifier in log messages.
 */
#ifndef LOG_TAG
#define LOG_TAG "App"
#endif

/*
 * Logging Macros Logic:
 * 1. Carriage Return (\r): Included at the START of the string.
 *    - Why: In "Raw Mode" (used by shells like Linenoise), the cursor may remain
 *      at the end of the previous output (e.g., after a prompt or partial write).
 *      `\r` forces the cursor to column 0 to prevent "staircase" output artifacts.
 *
 * 2. Tag Display: Fixed width [%-7s] ensures alignment across different modules.
 *
 * 3. ANSI Colors:
 *    - WARN:  Yellow (\033[1;33m)
 *    - ERROR: Red    (\033[1;31m)
 *    - RESET: Clear  (\033[0m)
 */

/**
 * @brief Log an informational message.
 * @param fmt Printf-style format string.
 * @param ... Variable arguments.
 */
#define LOG_INFO(fmt, ...) \
    printf("\r[%-7s] " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)

/**
 * @brief Log a warning message (Yellow).
 * @param fmt Printf-style format string.
 * @param ... Variable arguments.
 */
#define LOG_WARN(fmt, ...) \
    printf("\r\033[1;33m[%-7s] [WARN] " fmt "\033[0m\r\n", LOG_TAG, ##__VA_ARGS__)

/**
 * @brief Log an error message (Red).
 * @param fmt Printf-style format string.
 * @param ... Variable arguments.
 */
#define LOG_ERROR(fmt, ...) \
    printf("\r\033[1;31m[%-7s] [ERR ] " fmt "\033[0m\r\n", LOG_TAG, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */