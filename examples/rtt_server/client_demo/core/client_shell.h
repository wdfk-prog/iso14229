/**
 * @file client_shell.h
 * @brief Interactive Shell wrapper module header.
 * @details This header defines the public interface for the client's interactive
 *          command-line interface (CLI). It manages the main event loop,
 *          command history, autocompletion initialization, and remote path display.
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
#ifndef CLIENT_SHELL_H
#define CLIENT_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Shell Exit Codes
 * ========================================================================== */

/**
 * @brief Exit code indicating the user manually requested termination.
 * @details Returned when the user types 'exit' or presses Ctrl+D/Ctrl+C.
 */
#define SHELL_EXIT_USER     0

/**
 * @brief Exit code indicating the shell terminated due to a timeout.
 * @details Returned when the heartbeat mechanism fails consecutively (e.g., connection lost).
 */
#define SHELL_EXIT_TIMEOUT  -1

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Initializes the interactive shell settings.
 * @details Sets up Linenoise callbacks (completion, hints), loads command history,
 *          and registers built-in local commands (e.g., help, exit).
 */
void client_shell_init(void);

/**
 * @brief Enters the main interactive loop.
 * @details This function blocks until the shell session ends. It handles:
 *          - Non-blocking user input (via select).
 *          - Dispatching commands to the registry or remote console.
 *          - Polling the UDS stack.
 *          - Managing heartbeat keep-alive messages.
 *
 * @return int The exit reason:
 *         - SHELL_EXIT_USER (0): Normal exit.
 *         - SHELL_EXIT_TIMEOUT (-1): Connection lost.
 */
int client_shell_loop(void);

/**
 * @brief Updates the current remote working directory displayed in the prompt.
 * @details Used to reflect directory changes (cd) or initial sync state.
 *          The path is stored internally and used during prompt generation.
 *
 * @param path The new path string (e.g., "/flash/data").
 */
void client_shell_set_path(const char *path);

/**
 * @brief Retrieves the current remote working directory.
 *
 * @return const char* Pointer to the internal path string.
 */
const char* client_shell_get_path(void);

#ifdef __cplusplus
}
#endif

#endif /* CLIENT_SHELL_H */