/**
 * @file client_shell_windows.c
 * @brief Windows shell implementation for the formal client target.
 * @details Adds history, lightweight tab completion, prompt redraw, and
 *          console mode restoration on top of the Windows backend.
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

#define HISTORY_FILE            ".uds_history"
#define POLL_INTERVAL_MS        20U
#define SHELL_HISTORY_MAX       128U
#define SHELL_MATCH_MAX         64U

static volatile int g_shell_force_exit = 0;
static char g_remote_path[128] = "/";
static char g_input_line[CMD_MAX_LINE];
static size_t g_input_len = 0U;
static int g_prompt_visible = 0;
static size_t g_last_render_width = 0U;

static char *g_history[SHELL_HISTORY_MAX];
static size_t g_history_count = 0U;
static int g_history_index = -1;
static int g_history_saved_current = 0;
static char g_history_current[CMD_MAX_LINE];

static char g_completion_items[SHELL_MATCH_MAX][CMD_MAX_LINE];
static size_t g_completion_count = 0U;
static int g_completion_truncated = 0;

static void shell_history_reset_browse(void);
static void shell_render_current_line(void);

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

static void shell_get_prompt(char *prompt, size_t prompt_size)
{
    if (prompt == NULL || prompt_size == 0U) {
        return;
    }

    (void)snprintf(prompt, prompt_size, "msh %s> ", g_remote_path);
}

static void shell_clear_rendered_line(void)
{
    if (platform_console_supports_vt()) {
        (void)fputs("\r\x1b[2K", stdout);
    } else {
        size_t i;

        (void)fputc('\r', stdout);
        for (i = 0U; i < g_last_render_width; ++i) {
            (void)fputc(' ', stdout);
        }
        (void)fputc('\r', stdout);
    }
}

static void shell_set_input_line(const char *line)
{
    size_t len = 0U;

    if (line != NULL) {
        len = strlen(line);
        if (len >= sizeof(g_input_line)) {
            len = sizeof(g_input_line) - 1U;
        }
        memcpy(g_input_line, line, len);
    }

    g_input_line[len] = '\0';
    g_input_len = len;
}

static void shell_reset_input(void)
{
    shell_set_input_line("");
}

static void shell_render_current_line(void)
{
    char prompt[160];
    size_t prompt_len;

    shell_get_prompt(prompt, sizeof(prompt));
    prompt_len = strlen(prompt);

    shell_clear_rendered_line();
    (void)fwrite(prompt, 1U, prompt_len, stdout);
    if (g_input_len > 0U) {
        (void)fwrite(g_input_line, 1U, g_input_len, stdout);
    }
    platform_console_flush_stdout();

    g_prompt_visible = 1;
    g_last_render_width = prompt_len + g_input_len;
}

static void shell_hide_prompt(void)
{
    if (g_prompt_visible) {
        shell_clear_rendered_line();
        platform_console_flush_stdout();
    }
    g_prompt_visible = 0;
    g_last_render_width = 0U;
}

static void shell_history_free_all(void)
{
    size_t i;

    for (i = 0U; i < g_history_count; ++i) {
        free(g_history[i]);
        g_history[i] = NULL;
    }

    g_history_count = 0U;
    shell_history_reset_browse();
}

static int shell_history_push_owned(char *entry)
{
    size_t i;

    if (entry == NULL || entry[0] == '\0') {
        free(entry);
        return 0;
    }

    if (g_history_count > 0U && strcmp(g_history[g_history_count - 1U], entry) == 0) {
        free(entry);
        return 0;
    }

    if (g_history_count == SHELL_HISTORY_MAX) {
        free(g_history[0]);
        for (i = 1U; i < g_history_count; ++i) {
            g_history[i - 1U] = g_history[i];
        }
        g_history_count--;
    }

    g_history[g_history_count++] = entry;
    return 0;
}

static int shell_history_add(const char *line)
{
    return shell_history_push_owned(shell_strdup(line));
}

static void shell_history_save(void)
{
    FILE *fp;
    size_t i;

    fp = fopen(HISTORY_FILE, "w");
    if (fp == NULL) {
        return;
    }

    for (i = 0U; i < g_history_count; ++i) {
        (void)fputs(g_history[i], fp);
        (void)fputc('\n', fp);
    }

    (void)fclose(fp);
}

static void shell_history_load(void)
{
    FILE *fp;
    char line[CMD_MAX_LINE];

    shell_history_free_all();

    fp = fopen(HISTORY_FILE, "r");
    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strcspn(line, "\r\n");
        char *copy;

        line[len] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        copy = shell_strdup(line);
        if (copy == NULL) {
            break;
        }
        (void)shell_history_push_owned(copy);
    }

    (void)fclose(fp);
}

static void shell_history_reset_browse(void)
{
    g_history_index = -1;
    g_history_saved_current = 0;
    g_history_current[0] = '\0';
}

static void shell_history_apply_current(void)
{
    if (g_history_index >= 0 && (size_t)g_history_index < g_history_count) {
        shell_set_input_line(g_history[g_history_index]);
    } else if (g_history_saved_current) {
        shell_set_input_line(g_history_current);
    } else {
        shell_reset_input();
    }

    shell_render_current_line();
}

static void shell_history_prev(void)
{
    if (g_history_count == 0U) {
        return;
    }

    if (g_history_index < 0) {
        memcpy(g_history_current, g_input_line, g_input_len + 1U);
        g_history_saved_current = 1;
        g_history_index = (int)g_history_count - 1;
    } else if (g_history_index > 0) {
        g_history_index--;
    }

    shell_history_apply_current();
}

static void shell_history_next(void)
{
    if (g_history_index < 0) {
        return;
    }

    if ((size_t)(g_history_index + 1) < g_history_count) {
        g_history_index++;
    } else {
        g_history_index = -1;
    }

    shell_history_apply_current();
}

static void shell_completion_reset(void)
{
    g_completion_count = 0U;
    g_completion_truncated = 0;
}

static int shell_completion_contains(const char *value)
{
    size_t i;

    for (i = 0U; i < g_completion_count; ++i) {
        if (strcmp(g_completion_items[i], value) == 0) {
            return 1;
        }
    }

    return 0;
}

static void shell_completion_add(const char *value)
{
    size_t len;

    if (value == NULL || value[0] == '\0') {
        return;
    }
    if (shell_completion_contains(value)) {
        return;
    }
    if (g_completion_count >= SHELL_MATCH_MAX) {
        g_completion_truncated = 1;
        return;
    }

    len = strlen(value);
    if (len >= CMD_MAX_LINE) {
        len = CMD_MAX_LINE - 1U;
    }

    memcpy(g_completion_items[g_completion_count], value, len);
    g_completion_items[g_completion_count][len] = '\0';
    g_completion_count++;
}

static void shell_collect_command_completions(const char *prefix)
{
    int count;
    size_t prefix_len = strlen(prefix);
    int i;

    count = cmd_get_count();
    for (i = 0; i < count; ++i) {
        const char *name = cmd_get_name(i);
        if (name != NULL && strncmp(prefix, name, prefix_len) == 0) {
            shell_completion_add(name);
        }
    }

    count = client_console_get_cmd_count();
    for (i = 0; i < count; ++i) {
        const char *name = client_console_get_cmd_name(i);
        if (name != NULL && strncmp(prefix, name, prefix_len) == 0) {
            shell_completion_add(name);
        }
    }
}

static void shell_collect_argument_completions(const char *line)
{
    const char *word_part = strrchr(line, ' ');
    size_t prefix_len;
    size_t word_len;
    int count;
    int i;

    if (word_part == NULL) {
        return;
    }

    word_part += 1;
    prefix_len = (size_t)(word_part - line);
    word_len = strlen(word_part);

    count = client_console_get_file_count();
    for (i = 0; i < count; ++i) {
        const char *fname = client_console_get_file_name(i);
        char completion[CMD_MAX_LINE];
        size_t fname_len;

        if (fname == NULL || strncmp(word_part, fname, word_len) != 0) {
            continue;
        }

        fname_len = strlen(fname);
        if (prefix_len + fname_len >= sizeof(completion)) {
            continue;
        }

        memcpy(completion, line, prefix_len);
        memcpy(completion + prefix_len, fname, fname_len + 1U);
        shell_completion_add(completion);
    }
}

static size_t shell_completion_common_prefix_len(void)
{
    size_t prefix_len;
    size_t i;

    if (g_completion_count == 0U) {
        return 0U;
    }

    prefix_len = strlen(g_completion_items[0]);
    for (i = 1U; i < g_completion_count; ++i) {
        size_t j = 0U;
        while (j < prefix_len && g_completion_items[0][j] == g_completion_items[i][j]) {
            ++j;
        }
        prefix_len = j;
    }

    return prefix_len;
}

static void shell_print_completions(void)
{
    size_t i;

    shell_hide_prompt();
    (void)fputc('\n', stdout);
    for (i = 0U; i < g_completion_count; ++i) {
        (void)fputs(g_completion_items[i], stdout);
        (void)fputs("\n", stdout);
    }
    if (g_completion_truncated) {
        (void)fputs("...\n", stdout);
    }
    platform_console_flush_stdout();
}

static void shell_complete_current_input(void)
{
    size_t common_len;
    int has_space;

    shell_completion_reset();
    has_space = (strchr(g_input_line, ' ') != NULL);

    if (has_space) {
        shell_collect_argument_completions(g_input_line);
    } else {
        shell_collect_command_completions(g_input_line);
    }

    if (g_completion_count == 0U) {
        (void)fputc('\a', stdout);
        platform_console_flush_stdout();
        return;
    }

    if (g_completion_count == 1U) {
        shell_set_input_line(g_completion_items[0]);
        if (!has_space && g_input_len + 1U < sizeof(g_input_line)) {
            g_input_line[g_input_len++] = ' ';
            g_input_line[g_input_len] = '\0';
        }
        shell_render_current_line();
        return;
    }

    common_len = shell_completion_common_prefix_len();
    if (common_len > g_input_len) {
        char expanded[CMD_MAX_LINE];

        memcpy(expanded, g_completion_items[0], common_len);
        expanded[common_len] = '\0';
        shell_set_input_line(expanded);
        shell_render_current_line();
        return;
    }

    shell_print_completions();
    shell_render_current_line();
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

        if (g_prompt_visible) {
            shell_render_current_line();
        }
    }
}

const char *client_shell_get_path(void)
{
    return g_remote_path;
}

void client_shell_async_write(const uint8_t *data, size_t len)
{
    int was_prompt_visible;

    if (data == NULL || len == 0U) {
        return;
    }

    was_prompt_visible = g_prompt_visible;
    if (was_prompt_visible) {
        shell_hide_prompt();
    }

    (void)fwrite(data, 1U, len, stdout);
    if (data[len - 1U] != '\n' && data[len - 1U] != '\r') {
        (void)fputs("\r\n", stdout);
    }
    platform_console_flush_stdout();

    if (was_prompt_visible) {
        shell_render_current_line();
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
    shell_history_load();
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
        (void)shell_history_add(line);
        shell_history_save();

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
    shell_history_reset_browse();
    return 0;
}

static int shell_handle_extended_key(char key)
{
    switch ((unsigned char)key) {
    case 72U:
        shell_history_prev();
        return 0;
    case 80U:
        shell_history_next();
        return 0;
    default:
        return 0;
    }
}

static int shell_handle_input_char(char ch, uint32_t *last_heartbeat_ts, int *exit_code)
{
    if (last_heartbeat_ts == NULL || exit_code == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ch == '\r' || ch == '\n') {
        int cmd_res;

        shell_hide_prompt();
        (void)fputc('\n', stdout);
        g_input_line[g_input_len] = '\0';
        cmd_res = shell_execute_completed_line(g_input_line, last_heartbeat_ts, exit_code);
        shell_reset_input();
        return cmd_res;
    }

    if ((unsigned char)ch == 0U || (unsigned char)ch == 0xE0U) {
        char ext = 0;
        if (platform_console_read_char(&ext) > 0) {
            return shell_handle_extended_key(ext);
        }
        return 0;
    }

    if (ch == 3 || ch == 4 || ch == 26) {
        shell_hide_prompt();
        printf("\nQuit\n");
        *exit_code = SHELL_EXIT_USER;
        return 1;
    }

    if (ch == '\t') {
        shell_complete_current_input();
        return 0;
    }

    if (ch == '\b' || (unsigned char)ch == 127U) {
        if (g_input_len > 0U) {
            g_input_len--;
            g_input_line[g_input_len] = '\0';
            shell_history_reset_browse();
            shell_render_current_line();
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
    shell_history_reset_browse();
    shell_render_current_line();
    return 0;
}

int client_shell_loop(void)
{
    uint32_t last_heartbeat_ts = sys_tick_get_ms();
    int exit_code = SHELL_EXIT_USER;

    if (platform_console_prepare_interactive() != 0) {
        LOG_ERROR("platform_console_prepare_interactive failed: %d", errno);
        return SHELL_EXIT_USER;
    }

    g_shell_force_exit = 0;
    shell_history_reset_browse();
    shell_reset_input();
    g_prompt_visible = 0;
    g_last_render_width = 0U;

    printf("\n[Shell] Interactive Mode Started. Type 'help' or 'exit'.\n");
    shell_render_current_line();

    while (1) {
        int ret;

        if (g_shell_force_exit) {
            shell_hide_prompt();
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

            shell_hide_prompt();
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
                    shell_hide_prompt();
                    LOG_ERROR("shell input handling failed: %d", errno);
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
                if (cmd_res > 0) {
                    break;
                }
                if (!g_prompt_visible) {
                    shell_render_current_line();
                }
            } else if (read_res == 0) {
                shell_hide_prompt();
                printf("\nQuit\n");
                exit_code = SHELL_EXIT_USER;
                break;
            } else {
                int shell_err = 0;
                platform_shell_input_action_t action = platform_shell_input_classify_last_error(&shell_err);

                if (action == PLATFORM_SHELL_INPUT_ACTION_USER_EXIT) {
                    shell_hide_prompt();
                    printf("\nQuit\n");
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
                if (action == PLATFORM_SHELL_INPUT_ACTION_IO_ERROR) {
                    shell_hide_prompt();
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

    shell_hide_prompt();
    shell_history_reset_browse();
    shell_reset_input();
    platform_console_restore_interactive();
    return exit_code;
}
