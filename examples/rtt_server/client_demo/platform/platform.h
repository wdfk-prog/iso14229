/**
 * @file platform.h
 * @brief Minimal platform abstraction for client_demo.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Return a monotonic timestamp in milliseconds.
 */
uint32_t platform_tick_ms(void);

/**
 * @brief Sleep current thread for the specified milliseconds.
 */
void platform_sleep_ms(uint32_t ms);

/**
 * @brief Poll console input readiness on stdin.
 *
 * @param timeout_ms Timeout in milliseconds.
 * @return >0 when input is ready, 0 on timeout, -1 on error.
 */
int platform_console_poll_input(uint32_t timeout_ms);

/**
 * @brief Read a single character from stdin.
 *
 * @param ch Output character pointer.
 * @return 1 when one character is read, 0 on EOF, -1 on error.
 */
int platform_console_read_char(char *ch);

/**
 * @brief Flush stdout immediately.
 */
void platform_console_flush_stdout(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
