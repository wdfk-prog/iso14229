/**
 * @file core/client.h
 * @brief Declares public client service registration and helper APIs.
 * @details This header provides the entry points used by the application layer
 *          to register local service handlers, execute common UDS workflows,
 *          and query remote-console caches.
 */
#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>

/**
 * @brief Register Service 0x10 local command handlers.
 */
void client_0x10_init(void);
/**
 * @brief Register Service 0x27 local command handlers.
 */
void client_0x27_init(void);
/**
 * @brief Register Service 0x31 local command handlers.
 */
void client_0x31_init(void);
/**
 * @brief Register Service 0x22 and 0x2E local command handlers.
 */
void client_0x22_0x2E_init(void);
/**
 * @brief Register Service 0x28 local command handlers.
 */
void client_0x28_init(void);
/**
 * @brief Register Service 0x11 local command handlers.
 */
void client_0x11_init(void);
/**
 * @brief Register file-transfer service local command handlers.
 */
void client_file_svc_init(void);
/**
 * @brief Register Service 0x2F local command handlers.
 */
void client_0x2F_init(void);
/**
 * @brief Register Service 0x2A ULOG local command handlers.
 */
void client_0x2A_init(void);

/**
 * @brief Request a diagnostic session change.
 * @param session_type Requested UDS session type.
 * @return 0 on success, -1 on failure.
 */
int client_request_session(uint8_t session_type);
/**
 * @brief Perform security access for a target level.
 * @param level Requested seed/key level.
 * @return 0 on success, -1 on failure.
 */
int client_perform_security(uint8_t level);
/**
 * @brief Send a remote console command via UDS routine control.
 * @param cmd_str Command string to execute on the remote target.
 * @return 0 on success, -1 on failure.
 */
int client_send_console_command(const char *cmd_str);
/**
 * @brief Synchronize remote command cache.
 * @return 0 on success, -1 on failure.
 */
int client_sync_remote_commands(void);
/**
 * @brief Auto-enable the 0x2A ULOG stream using stored defaults.
 * @return 0 on success, -1 on failure.
 */
int client_0x2A_ulog_auto_start(void);
/**
 * @brief Auto-disable the 0x2A ULOG stream (stop-all).
 * @return 0 on success, -1 on failure.
 */
int client_0x2A_ulog_auto_stop(void);

/* Console API */
/**
 * @copydoc client_send_console_command(const char *cmd_str)
 */
int client_send_console_command(const char *cmd_str);

/**
 * @brief Get the number of cached remote command names.
 * @return Number of cached command entries.
 */
int client_console_get_cmd_count(void);
/**
 * @brief Get one cached remote command name by index.
 * @param index Zero-based cache index.
 * @return Pointer to command name, or NULL when index is out of range.
 */
const char* client_console_get_cmd_name(int index);

/**
 * @brief Get the number of cached remote file names.
 * @return Number of cached file entries.
 */
int client_console_get_file_count(void);
/**
 * @brief Get one cached remote file name by index.
 * @param index Zero-based cache index.
 * @return Pointer to file name, or NULL when index is out of range.
 */
const char* client_console_get_file_name(int index);

#endif /* CLIENT_H */
