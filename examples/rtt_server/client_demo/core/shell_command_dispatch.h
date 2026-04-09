#ifndef SHELL_COMMAND_DISPATCH_H
#define SHELL_COMMAND_DISPATCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*shell_local_execute_fn)(char *line);
typedef int (*shell_remote_send_fn)(const char *line);

/**
 * @brief Trim leading/trailing ASCII whitespace in place.
 * @param line Mutable command buffer.
 * @return char* Pointer to the trimmed string (may point inside @p line).
 */
char *shell_trim_whitespace_inplace(char *line);

/**
 * @brief Dispatch one interactive shell line.
 * @details Trims the line, treats `exit` as a local shell escape, tries local
 *          execution first on a temporary copy, and falls back to remote send
 *          only when the local registry reports CMD_EXEC_NOT_FOUND.
 *
 * @param line Mutable command buffer. The string is trimmed in place.
 * @param exec_local Local command executor, typically cmd_execute_line().
 * @param send_remote Remote passthrough sender, typically client_send_console_command().
 * @param exit_requested Output flag set to 1 when the trimmed command is `exit`.
 * @return int Command result from the chosen execution path.
 */
int shell_dispatch_command_line(char *line,
                                shell_local_execute_fn exec_local,
                                shell_remote_send_fn send_remote,
                                int *exit_requested);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_COMMAND_DISPATCH_H */
