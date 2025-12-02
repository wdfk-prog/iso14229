/**
 * @file core/client.h
 */
#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>

/* ... 其他初始化函数 ... */
void client_0x10_init(void);
void client_0x27_init(void);
void client_0x31_init(void);
void client_0x22_0x2E_init(void);
void client_0x28_init(void);
void client_0x11_init(void);
void client_file_svc_init(void);
void client_0x2F_init(void);

int client_request_session(uint8_t session_type);
int client_perform_security(uint8_t level);
int client_send_console_command(const char *cmd_str);
int client_sync_remote_commands(void);

/* Console API */
int client_send_console_command(const char *cmd_str);

/* [新增] 远程命令缓存 (Help) */
int client_console_get_cmd_count(void);
const char* client_console_get_cmd_name(int index);

/* [新增] 远程文件缓存 (LS) */
int client_console_get_file_count(void);
const char* client_console_get_file_name(int index);

#endif /* CLIENT_H */