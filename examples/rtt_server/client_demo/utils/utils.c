/**
 * @file utils.c
 * @file utils.c
 * @brief Utility function implementations for system time, delay, and data integrity.
 * @details This module encapsulates platform-specific system calls (POSIX) and 
 *          standard algorithms (CRC32, UI rendering) to provide a unified interface 
 *          for the application layer.
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
#include "utils.h"
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

/* ==========================================================================
 * Configuration Macros
 * ========================================================================== */

/** @brief Width of the progress bar in characters (excluding labels and percentages). */
#define PB_WIDTH 40

/* ==========================================================================
 * Implementation
 * ========================================================================== */

/**
 * @brief Retrieves the current system monotonic time in milliseconds.
 * 
 * @details This function uses `gettimeofday` to acquire the system time with 
 *          microsecond resolution and downsamples it to milliseconds.
 *          @note This relies on the system clock. If the system clock changes 
 *          discontinuously (e.g., NTP update), the return value may jump.
 * 
 * @return uint32_t The current timestamp in milliseconds.
 */
uint32_t sys_tick_get_ms(void) 
{
    struct timeval tv;
    
    /* 
     * Retrieve current time. 
     * NULL is passed as the second argument because timezone information is not required.
     */
    gettimeofday(&tv, NULL);

    /* Convert seconds and microseconds to total milliseconds */
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

/**
 * @brief Suspends the execution of the calling thread.
 * 
 * @param ms The duration to sleep in milliseconds.
 */
void sys_delay_ms(uint32_t ms) 
{
    /* 
     * usleep accepts duration in microseconds.
     * Conversion: 1 ms = 1000 us.
     */
    usleep(ms * 1000);
}

/**
 * @brief Calculates the CRC32 checksum of a data buffer.
 * 
 * @details This implementation uses the standard CRC-32 algorithm (ISO 3309).
 *          - Polynomial: 0x04C11DB7
 *          - Reversed Polynomial (for LSB-first processing): 0xEDB88320
 *          - Initial Value: 0xFFFFFFFF (handled via pre-inversion)
 *          - Final XOR: 0xFFFFFFFF (handled via post-inversion)
 * 
 *          This bit-wise implementation avoids the need for a large lookup table,
 *          saving memory at the cost of CPU cycles.
 * 
 * @param crc  The initial CRC value. Pass 0 for the first block. 
 *             For chained calls, pass the return value of the previous call.
 * @param data Pointer to the input data buffer.
 * @param len  Length of the input data in bytes.
 * 
 * @return uint32_t The calculated CRC32 checksum.
 */
uint32_t crc32_calc(uint32_t crc, const uint8_t *data, size_t len) 
{
    /* 
     * Pre-invert the CRC register.
     * If 'crc' is 0 (start), this sets the register to 0xFFFFFFFF.
     * If 'crc' is a result of a previous call (chained), this inverts the 
     * post-inverted result back to the raw register state for continuation.
     */
    crc = ~crc;

    while (len--) {
        /* XOR the input byte into the low byte of the CRC register */
        crc ^= *data++;

        /* Process 8 bits for the current byte */
        for (int k = 0; k < 8; k++) {
            /*
             * Check the Least Significant Bit (LSB).
             * If LSB is 1: Shift right and XOR with the reversed polynomial (0xEDB88320).
             * If LSB is 0: Just shift right.
             */
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }

    /* Post-invert the result (Final XOR Value: 0xFFFFFFFF) */
    return ~crc;
}

/**
 * @brief Renders a text-based progress bar to stdout.
 * 
 * @details Visual format: [Label] [=====>    ] 50% (500/1000)
 *          Uses ANSI escape codes (\r, \033[K) to overwrite the current line,
 *          creating an animation effect.
 * 
 * @param current Current progress value (e.g., bytes transferred).
 * @param total   Total expected value.
 * @param label   Short description string displayed before the bar.
 */
void utils_render_progress(size_t current, size_t total, const char *label) 
{
    float percent = 0.0f;
    int val;
    int lpad;
    int rpad;
    int i;

    /* Calculate percentage, protecting against division by zero */
    if (total > 0) {
        percent = (float)current / total;
    }
    
    /* Clamp percentage to maximum 1.0 (100%) to prevent visual overflow */
    if (percent > 1.0f) {
        percent = 1.0f;
    }

    /* Calculate bar dimensions */
    val  = (int)(percent * 100);
    lpad = (int)(percent * PB_WIDTH);
    rpad = PB_WIDTH - lpad;

    /* 
     * \r: Carriage Return - Move cursor to start of line.
     * \033[K: ANSI Clear Line - Erase from cursor to end of line.
     * This ensures artifacts from longer previous lines don't remain.
     */
    printf("\r\033[K"); 
    
    if (label) {
        printf("%s ", label);
    }
    
    /* Render the bar: [=====>    ] */
    printf("[");
    for (i = 0; i < lpad; i++) {
        putchar('=');
    }
    
    if (lpad < PB_WIDTH) {
        putchar('>'); /* Current head indicator */
    }
    
    for (i = 0; i < rpad - 1; i++) {
        putchar(' '); /* Empty space */
    }
    
    /* Show numerical percentage */
    printf("] %3d%%", val);
    
    /* Optionally show raw values */
    if (total > 0) {
        printf(" (%lu/%lu)", (unsigned long)current, (unsigned long)total);
    }

    /* Force stdout flush to update the terminal immediately */
    fflush(stdout);
}