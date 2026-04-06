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

typedef enum {
    PLATFORM_SHELL_INPUT_ACTION_CONTINUE = 0,
    PLATFORM_SHELL_INPUT_ACTION_USER_EXIT = 1,
    PLATFORM_SHELL_INPUT_ACTION_IO_ERROR = 2,
} platform_shell_input_action_t;

/**
 * @brief Return a monotonic timestamp in milliseconds.
 */
uint32_t platform_tick_ms(void);

/**
 * @brief Sleep current thread for the specified milliseconds.
 */
void platform_sleep_ms(uint32_t ms);

/**
 * @brief Prepare the console for interactive shell I/O.
 *
 * Implementations may enable VT output, switch stdin into character mode,
 * and register restoration hooks. Platforms that do not need special setup
 * should return success.
 *
 * @return 0 on success, -1 on failure.
 */
int platform_console_prepare_interactive(void);

/**
 * @brief Restore console state saved by platform_console_prepare_interactive().
 */
void platform_console_restore_interactive(void);

/**
 * @brief Report whether VT output sequences are available on the active console.
 *
 * @return 1 when VT output is enabled, 0 otherwise.
 */
int platform_console_supports_vt(void);

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
 * @brief Get stdin file descriptor used by console backend.
 */
int platform_console_stdin_fd(void);

/**
 * @brief Get stdout file descriptor used by console backend.
 */
int platform_console_stdout_fd(void);

/**
 * @brief Classify last shell input error into continue/user-exit/io-error actions and optionally return errno snapshot.
 */
platform_shell_input_action_t platform_shell_input_classify_last_error(int *err_out);

/**
 * @brief Flush stdout immediately.
 */
void platform_console_flush_stdout(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
