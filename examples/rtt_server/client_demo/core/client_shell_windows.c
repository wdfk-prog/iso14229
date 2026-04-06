/**
 * @file client_shell_windows.c
 * @brief Minimal Windows shell implementation for the formal client target.
 * @details Uses the existing platform abstraction instead of linenoise so the
 *          native Windows build does not depend on POSIX termios support.
 */
#define LOG_TAG "Shell"

#include "client_shell.h"
#include "cmd_registry.h"
#include "uds_context.h"
#include "client_config.h"
#include "client.h"
#include "platform.h"
#include "../utils/utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_INTERVAL_MS        20U

static volatile int g_shell_force_exit = 0;
static char g_remote_path[128] = "/";
static char g_input_line[CMD_MAX_LINE];
static size_t g_input_len = 0U;
static int g_prompt_visible = 0;

static void shell_reset_input(void)
{
    g_input_len = 0U;
    g_input_line[0] = '\0';
}

static void shell_print_prompt(void)
{
    printf("msh %s> ", g_remote_path);
    fflush(stdout);
    g_prompt_visible = 1;
}

static void client_on_disconnect(void)
{
    g_shell_force_exit = 1;
}

static char *shell_strdup(const char *src)
{
    size_t len;
    char *copy;

    if (src == NULL) {
        return NULL;
    }

    len = strlen(src) + 1U;
    copy = (char *)malloc(len);
    if (copy != NULL) {
        memcpy(copy, src, len);
    }
    return copy;
}

void client_shell_set_path(const char *path)
{
    if (path == NULL) {
        return;
    }

    if (strlen(path) < sizeof(g_remote_path)) {
        size_t len = strlen(path);

        strncpy(g_remote_path, path, sizeof(g_remote_path) - 1U);
        g_remote_path[sizeof(g_remote_path) - 1U] = '\0';

        if (len > 0U && g_remote_path[len - 1U] == ':') {
            g_remote_path[len - 1U] = '\0';
        }
    }
}

const char *client_shell_get_path(void)
{
    return g_remote_path;
}

void client_shell_async_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U) {
        return;
    }

    if (g_prompt_visible) {
        printf("\r\n");
    }

    (void)fwrite(data, 1U, len, stdout);
    platform_console_flush_stdout();

    if (g_prompt_visible) {
        if (data[len - 1U] != '\n' && data[len - 1U] != '\r') {
            printf("\r\n");
        }
        shell_print_prompt();
        if (g_input_len > 0U) {
            (void)fwrite(g_input_line, 1U, g_input_len, stdout);
            platform_console_flush_stdout();
        }
    }
}

static int handle_help_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n[Local Commands]\n");
    cmd_print_help();

    printf("\n[Remote Commands]\n");
    return client_send_console_command("help");
}

int client_sync_remote_commands(void)
{
    return handle_help_cmd(0, NULL);
}

void client_shell_init(void)
{
    cmd_register("help", handle_help_cmd, "Show Local & Remote Help", "");
    cmd_register("exit", NULL, "Exit Shell", "");
    uds_register_disconnect_callback(client_on_disconnect);
}

static int shell_execute_completed_line(char *line, uint32_t *last_heartbeat_ts, int *exit_code)
{
    if (line == NULL || last_heartbeat_ts == NULL || exit_code == NULL) {
        return -1;
    }

    if (line[0] != '\0') {
        if (strcmp(line, "exit") == 0) {
            *exit_code = SHELL_EXIT_USER;
            return 1;
        }

        if (strcmp(line, "help") == 0) {
            (void)cmd_execute_line(line);
        } else {
            char *line_copy = shell_strdup(line);
            if (line_copy != NULL) {
                int res = cmd_execute_line(line_copy);
                if (res == -1) {
                    (void)client_send_console_command(line);
                }
                free(line_copy);
            } else {
                (void)client_send_console_command(line);
            }
        }
    }

    *last_heartbeat_ts = sys_tick_get_ms();
    return 0;
}

static int shell_handle_input_char(char ch, uint32_t *last_heartbeat_ts, int *exit_code)
{
    if (last_heartbeat_ts == NULL || exit_code == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ch == '\r' || ch == '\n') {
        int cmd_res;

        putchar('\n');
        g_prompt_visible = 0;
        g_input_line[g_input_len] = '\0';
        cmd_res = shell_execute_completed_line(g_input_line, last_heartbeat_ts, exit_code);
        shell_reset_input();
        return cmd_res;
    }

    if ((unsigned char)ch == 0U || (unsigned char)ch == 0xE0U) {
        char ignored = 0;
        (void)platform_console_read_char(&ignored);
        return 0;
    }

    if (ch == 3 || ch == 4 || ch == 26) {
        printf("\nQuit\n");
        g_prompt_visible = 0;
        *exit_code = SHELL_EXIT_USER;
        return 1;
    }

    if (ch == '\b' || (unsigned char)ch == 127U) {
        if (g_input_len > 0U) {
            g_input_len--;
            g_input_line[g_input_len] = '\0';
            printf("\b \b");
            fflush(stdout);
        }
        return 0;
    }

    if (!isprint((unsigned char)ch)) {
        return 0;
    }

    if ((g_input_len + 1U) >= sizeof(g_input_line)) {
        return 0;
    }

    g_input_line[g_input_len++] = ch;
    g_input_line[g_input_len] = '\0';
    putchar(ch);
    fflush(stdout);
    return 0;
}

int client_shell_loop(void)
{
    uint32_t last_heartbeat_ts = sys_tick_get_ms();
    int exit_code = SHELL_EXIT_USER;

    g_shell_force_exit = 0;
    shell_reset_input();

    printf("\n[Shell] Interactive Mode Started. Type 'help' or 'exit'.\n");
    shell_print_prompt();

    while (1) {
        int ret;

        if (g_shell_force_exit) {
            printf("\r\n\033[1;31m[Fatal] Connection lost (Callback Triggered).\033[0m\r\n");
            exit_code = SHELL_EXIT_TIMEOUT;
            break;
        }

        ret = platform_console_poll_input(POLL_INTERVAL_MS);
        if (ret < 0) {
            int err = errno;

            if (err == EINTR) {
                continue;
            }

            LOG_ERROR("platform_console_poll_input failed: %d", err);
            exit_code = SHELL_EXIT_USER;
            break;
        }

        if (ret > 0) {
            char ch = 0;
            int read_res = platform_console_read_char(&ch);

            if (read_res > 0) {
                int cmd_res = shell_handle_input_char(ch, &last_heartbeat_ts, &exit_code);
                if (cmd_res < 0) {
                    LOG_ERROR("shell input handling failed: %d", errno);
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
                if (cmd_res > 0) {
                    break;
                }
                if (!g_prompt_visible) {
                    shell_print_prompt();
                }
            } else if (read_res == 0) {
                printf("\nQuit\n");
                exit_code = SHELL_EXIT_USER;
                break;
            } else {
                int shell_err = 0;
                platform_shell_input_action_t action = platform_shell_input_classify_last_error(&shell_err);

                if (action == PLATFORM_SHELL_INPUT_ACTION_USER_EXIT) {
                    printf("\nQuit\n");
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
                if (action == PLATFORM_SHELL_INPUT_ACTION_IO_ERROR) {
                    LOG_ERROR("platform_console_read_char failed: %d", shell_err);
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
            }
        }

        uds_poll();

        {
            uint32_t now = sys_tick_get_ms();
            if ((now - last_heartbeat_ts) > CLIENT_HEARTBEAT_MS) {
                int hb_res = uds_send_heartbeat_safe();
                if (hb_res == 0 || hb_res == -2) {
                    last_heartbeat_ts = now;
                }
            }
        }
    }

    g_prompt_visible = 0;
    shell_reset_input();
    return exit_code;
}
