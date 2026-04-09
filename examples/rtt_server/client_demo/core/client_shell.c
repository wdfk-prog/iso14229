/**
 * @file client_shell.c
 * @brief Interactive Shell Module.
 * @details Implements the CLI loop using Linenoise, handles command autocompletion,
 *          hints, history, and integration with the UDS context for heartbeat management.
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
#define LOG_TAG "Shell"

#include "client_shell.h"
#include "cmd_registry.h"
#include "uds_context.h"      /* For uds_register_disconnect_callback, uds_poll, etc. */
#include "client_config.h"    /* For CLIENT_HEARTBEAT_MS */
#include "client.h"           /* For client_console_get_... accessors */
#include "platform.h"
#include "shell_command_dispatch.h"
#include "../utils/linenoise.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

/* ==========================================================================
 * Configuration & Globals
 * ========================================================================== */

#define HISTORY_FILE            ".uds_history"
#define MAX_HEARTBEAT_RETRIES   3
#define POLL_INTERVAL_MS        20 /* 20ms polling interval */

/**
 * @brief Global Linenoise State.
 * @note  Must be global/static to be accessible by the disconnect callback
 *        for emergency cleanup.
 */
static struct linenoiseState g_ls;

/**
 * @brief Force Exit Flag.
 * @details Set by `client_on_disconnect` when the UDS context detects a broken link.
 */
static volatile int g_shell_force_exit = 0;

/**
 * @brief Whether linenoise editor is currently active.
 * @details This flag synchronizes async terminal output with linenoise lifecycle.
 *          The 0x2A unsolicited log path may call client_shell_async_write() at
 *          any time; hide/show must run only while editor is active.
 *          Without this guard, async logs can corrupt prompt rendering, break
 *          input echo, or trigger duplicated/stale EditStop state transitions.
 */
static volatile int g_shell_editor_active = 0;

/** @brief Current remote working directory for the prompt. */
static char g_remote_path[128] = "/";

static void shell_editor_stop(bool force_stop);

/* ==========================================================================
 * Callbacks & Helpers
 * ========================================================================== */

/**
 * @brief Disconnect Callback.
 * @details Registered with the UDS Context. Invoked when heartbeat fails repeatedly.
 *          It stops the line editor to restore terminal settings and signals the loop to exit.
 */
static void client_on_disconnect(void) 
{
    shell_editor_stop(false);

    /* Signal the main loop to break. */
    g_shell_force_exit = 1;
}

/**
 * @brief Sets the current remote path for the prompt.
 * @param path New path string.
 */
void client_shell_set_path(const char *path) 
{
    if (path && strlen(path) < sizeof(g_remote_path)) {
        size_t len = strlen(path);
        
        strncpy(g_remote_path, path, sizeof(g_remote_path) - 1);
        g_remote_path[sizeof(g_remote_path) - 1] = '\0';

        /* Remove trailing colon often found in RT-Thread 'ls' output headers */
        if (len > 0 && g_remote_path[len - 1] == ':') {
            g_remote_path[len - 1] = '\0';
        }
    }
}

/**
 * @brief Gets the current remote path.
 * @return const char* Pointer to path string.
 */
const char* client_shell_get_path(void) 
{
    return g_remote_path;
}

static void shell_editor_stop(bool force_stop)
{
    if (force_stop || g_shell_editor_active) {
        /*
         * linenoiseEditStop() currently emits a newline.
         * Keep stop behavior centralized here so future UX policy changes
         * do not require touching all shell control paths.
         */
        linenoiseEditStop(&g_ls);
    }

    g_shell_editor_active = 0;
}

static int shell_editor_start(char *buf, size_t buf_len, const char *prompt)
{
    if (linenoiseEditStart(&g_ls,
                           platform_console_stdin_fd(),
                           platform_console_stdout_fd(),
                           buf,
                           buf_len,
                           prompt) != 0) {
        shell_editor_stop(true); /* Conservative cleanup for partial start failures. */
        return -1;
    }

    g_shell_editor_active = 1;
    return 0;
}

/**
 * @brief Writes asynchronous output without breaking interactive prompt state.
 * @details Typical caller is the 0x2A unsolicited log stream. We gate
 *          linenoiseHide()/linenoiseShow() with g_shell_editor_active instead
 *          of calling them unconditionally so this function is safe in:
 *          1) active editing, 2) editor already stopped, 3) editor not started yet.
 */
void client_shell_async_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    if (g_shell_editor_active) {
        linenoiseHide(&g_ls);
    }

    (void)fwrite(data, 1, len, stdout);
    platform_console_flush_stdout();

    if (g_shell_editor_active) {
        linenoiseShow(&g_ls);
    }
}

/* --- Command Wrappers --- */

/**
 * @brief Wrapper for the 'help' command.
 * @details Displays local commands and triggers a remote help request.
 */
int handle_help_cmd(int argc, char **argv) 
{
    (void)argc; 
    (void)argv;
    
    printf("\n[Local Commands]\n");
    cmd_print_help();

    printf("\n[Remote Commands]\n");
    /* Send 'help' to server via 0x31 service; output handled by console handler */
    client_send_console_command("help");
    return 0;
}

/**
 * @brief Wrapper for the 'exit' command.
 * @details The main loop handles shell teardown; this handler only allows
 *          the command to participate in help output and tab completion.
 */
int handle_exit_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

/**
 * @brief Helper to trigger remote command sync (alias for help).
 */
int client_sync_remote_commands(void) 
{
    return handle_help_cmd(0, NULL);
}

/* --- Linenoise Callbacks --- */

/**
 * @brief Autocomplete callback.
 * @details Provides suggestions for commands and file arguments.
 */
static void add_prefixed_completion(linenoiseCompletions *lc,
                                  const char *buf,
                                  size_t prefix_len,
                                  const char *candidate)
{
    char full_completion[512];

    if (!candidate) {
        return;
    }

    if (prefix_len + strlen(candidate) >= sizeof(full_completion)) {
        return;
    }

    memcpy(full_completion, buf, prefix_len);
    strcpy(full_completion + prefix_len, candidate);
    linenoiseAddCompletion(lc, full_completion);
}

static void complete_remote_commands(const char *buf,
                                     linenoiseCompletions *lc,
                                     size_t prefix_len,
                                     const char *word_part)
{
    int count;
    int i;
    size_t word_len = strlen(word_part);

    count = client_console_get_cmd_count();
    for (i = 0; i < count; i++) {
        const char *name = client_console_get_cmd_name(i);
        if (name && strncmp(word_part, name, word_len) == 0) {
            add_prefixed_completion(lc, buf, prefix_len, name);
        }
    }
}

static void complete_remote_files(const char *buf,
                                  linenoiseCompletions *lc,
                                  size_t prefix_len,
                                  const char *word_part)
{
    int count;
    int i;
    size_t word_len = strlen(word_part);

    count = client_console_get_file_count();
    for (i = 0; i < count; i++) {
        const char *fname = client_console_get_file_name(i);
        if (fname && strncmp(word_part, fname, word_len) == 0) {
            add_prefixed_completion(lc, buf, prefix_len, fname);
        }
    }
}

static void complete_local_files(const char *buf,
                                 linenoiseCompletions *lc,
                                 size_t prefix_len,
                                 const char *word_part)
{
    char dir_part[256] = {0};
    char search_dir[256] = ".";
    char entry_prefix[256] = {0};
    const char *slash;
    DIR *dir;
    struct dirent *entry;

    slash = strrchr(word_part, '/');
    if (slash) {
        size_t dir_len = (size_t)(slash - word_part + 1);
        if (dir_len >= sizeof(dir_part)) {
            return;
        }
        memcpy(dir_part, word_part, dir_len);
        dir_part[dir_len] = '\0';
        strncpy(entry_prefix, slash + 1, sizeof(entry_prefix) - 1);
        entry_prefix[sizeof(entry_prefix) - 1] = '\0';

        if (dir_part[0] != '\0') {
            size_t search_len = dir_len;
            if (search_len > 1 && dir_part[search_len - 1] == '/') {
                search_len--;
            }
            if (search_len == 0) {
                strcpy(search_dir, "/");
            } else if (search_len < sizeof(search_dir)) {
                memcpy(search_dir, dir_part, search_len);
                search_dir[search_len] = '\0';
            } else {
                return;
            }
        }
    } else {
        strncpy(entry_prefix, word_part, sizeof(entry_prefix) - 1);
        entry_prefix[sizeof(entry_prefix) - 1] = '\0';
    }

    dir = opendir(search_dir);
    if (!dir) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char candidate[512];
        char full_path[512];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (strncmp(entry->d_name, entry_prefix, strlen(entry_prefix)) != 0) {
            continue;
        }

        if (dir_part[0] != '\0') {
            snprintf(candidate, sizeof(candidate), "%s%s", dir_part, entry->d_name);
            snprintf(full_path, sizeof(full_path), "%s/%s", search_dir, entry->d_name);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", entry->d_name);
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        }

        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t len = strlen(candidate);
            if (len + 1 < sizeof(candidate)) {
                candidate[len] = '/';
                candidate[len + 1] = '\0';
            }
        }

        add_prefixed_completion(lc, buf, prefix_len, candidate);
    }

    closedir(dir);
}

static int is_local_path_command(const char *cmd)
{
    return (strcmp(cmd, "sy") == 0 || strcmp(cmd, "lls") == 0);
}

static int is_remote_path_command(const char *cmd)
{
    return (strcmp(cmd, "ry") == 0 || strcmp(cmd, "cd") == 0);
}

static int is_local_command_name(const char *cmd)
{
    int i;
    int count = cmd_get_count();

    for (i = 0; i < count; i++) {
        const char *name = cmd_get_name(i);
        if (name && strcmp(cmd, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void completion_callback(const char *buf, linenoiseCompletions *lc) 
{
    size_t len = strlen(buf);
    int i;
    int count;
    char *last_space = strrchr(buf, ' ');
    
    if (last_space == NULL) {
        /* Case A: Command Completion */
        
        /* 1. Local Commands */
        count = cmd_get_count();
        for (i = 0; i < count; i++) {
            const char *name = cmd_get_name(i);
            if (strncmp(buf, name, len) == 0) {
                linenoiseAddCompletion(lc, name);
            }
        }

        /* 2. Remote Commands (Cached) */
        count = client_console_get_cmd_count();
        for (i = 0; i < count; i++) {
            const char *name = client_console_get_cmd_name(i);
            if (name && strncmp(buf, name, len) == 0) {
                linenoiseAddCompletion(lc, name);
            }
        }
    } else {
        char cmd_name[64] = {0};
        const char *word_part = last_space + 1;
        size_t prefix_len = (size_t)(word_part - buf);
        size_t cmd_len = (size_t)(last_space - buf);

        if (cmd_len >= sizeof(cmd_name)) {
            cmd_len = sizeof(cmd_name) - 1;
        }
        memcpy(cmd_name, buf, cmd_len);
        cmd_name[cmd_len] = '\0';

        /* Reduce to first token only. */
        char *cmd_end = strchr(cmd_name, ' ');
        if (cmd_end) {
            *cmd_end = '\0';
        }

        if (is_local_path_command(cmd_name)) {
            complete_local_files(buf, lc, prefix_len, word_part);
        } else if (is_remote_path_command(cmd_name)) {
            complete_remote_files(buf, lc, prefix_len, word_part);
        } else if (strcmp(cmd_name, "rexec") == 0) {
            complete_remote_commands(buf, lc, prefix_len, word_part);
            complete_remote_files(buf, lc, prefix_len, word_part);
        } else if (!is_local_command_name(cmd_name)) {
            /*
             * Direct remote command mode: complete both remote subcommands
             * and remote file/directory entries.
             */
            complete_remote_commands(buf, lc, prefix_len, word_part);
            complete_remote_files(buf, lc, prefix_len, word_part);
        }
    }
}

/**
 * @brief Hints callback.
 * @details Provides usage hints for known commands.
 */
static char *hints_callback(const char *buf, int *color, int *bold) 
{
    int count = cmd_get_count();
    for (int i = 0; i < count; i++) {
        const char *name = cmd_get_name(i);
        if (strcmp(buf, name) == 0) {
            const char *hint = cmd_get_hint(name);
            if (hint) {
                *color = 35; // Magenta
                *bold = 0;
                return (char *)hint; 
            }
        }
    }
    return NULL;
}

/* ==========================================================================
 * Initialization & Main Loop
 * ========================================================================== */

void client_shell_init(void) 
{
    /* Setup Linenoise */
    linenoiseSetCompletionCallback(completion_callback);
    linenoiseSetHintsCallback(hints_callback);
    linenoiseHistoryLoad(HISTORY_FILE);
    
    /* Register built-in shell commands */
    cmd_register("help", handle_help_cmd, "Show Local & Remote Help", "");
    cmd_register("exit", handle_exit_cmd, "Exit Shell", "");

    /* Register the disconnect observer with UDS Context */
    uds_register_disconnect_callback(client_on_disconnect);
}

int client_shell_loop(void) 
{
    char *line;
    char buf[CMD_MAX_LINE];
    char prompt[150];
    
    uint32_t last_heartbeat_ts = sys_tick_get_ms();
    int exit_code = SHELL_EXIT_USER;

    /* Reset exit flag on entry */
    g_shell_force_exit = 0;

    printf("\n[Shell] Interactive Mode Started. Type 'help' or 'exit'.\n");

    /* Initial prompt. */
    snprintf(prompt, sizeof(prompt), "msh %s> ", g_remote_path);
    if (shell_editor_start(buf, sizeof(buf), prompt) != 0) {
        LOG_ERROR("Failed to start line editor");
        return SHELL_EXIT_USER;
    }

    /* --- Event Loop --- */
    while (1) {
        /* 1. Check for Forced Exit (Heartbeat Failure) */
        if (g_shell_force_exit) {
            /* Note: linenoiseEditStop is called inside client_on_disconnect */
            printf("\r\n\033[1;31m[Fatal] Connection lost (Callback Triggered).\033[0m\r\n");
            exit_code = SHELL_EXIT_TIMEOUT;
            break;
        }

        /* 2. Poll input readiness */
        int ret = platform_console_poll_input(POLL_INTERVAL_MS);

        if (ret < 0) {
            int err = errno;

            if (err == EINTR) {
                continue;
            }

            LOG_ERROR("platform_console_poll_input failed: %d", err);
            exit_code = SHELL_EXIT_USER;
            break;
        }

        /* 3. Handle Input */
        if (ret > 0) {
            line = linenoiseEditFeed(&g_ls);
            
            if (line == linenoiseEditMore) {
                /* User is typing... */
            } 
            else if (line != NULL) {
                /* Complete line received */
                shell_editor_stop(false); /* Restore terminal. */
                
                (void)shell_trim_whitespace_inplace(line);
                if (strlen(line) > 0) {
                    int exit_requested = 0;

                    linenoiseHistoryAdd(line);
                    linenoiseHistorySave(HISTORY_FILE);

                    /*
                     * Try local first. Only fall back to remote 0x31 passthrough
                     * when there is no matching local handler at all.
                     * This preserves hijacked commands such as sy/ry even when
                     * the local handler reports an execution failure.
                     */
                    (void)shell_dispatch_command_line(line,
                                                      cmd_execute_line,
                                                      client_send_console_command,
                                                      &exit_requested);
                    if (exit_requested) {
                        free(line);
                        exit_code = SHELL_EXIT_USER;
                        break;
                    }
                }
                
                free(line);
                
                /* Reset heartbeat timer on user activity */
                last_heartbeat_ts = sys_tick_get_ms();

                /* Re-enable prompt. */
                snprintf(prompt, sizeof(prompt), "msh %s> ", g_remote_path);
                if (shell_editor_start(buf, sizeof(buf), prompt) != 0) {
                    LOG_ERROR("Failed to restart line editor");
                    exit_code = SHELL_EXIT_USER;
                    break;
                }
            } else {
                int shell_err = 0;
                platform_shell_input_action_t action = platform_shell_input_classify_last_error(&shell_err);

                if (action == PLATFORM_SHELL_INPUT_ACTION_USER_EXIT) {
                    shell_editor_stop(false);
                    printf("\nQuit\n");
                    exit_code = SHELL_EXIT_USER;
                    break;
                } else if (action == PLATFORM_SHELL_INPUT_ACTION_IO_ERROR) {
                    LOG_ERROR("linenoiseEditFeed failed: %d", shell_err);
                    exit_code = SHELL_EXIT_USER;
                    break;
                } else {
                    continue;
                }
            }
        }

        /* 4. Poll UDS Stack */
        uds_poll();

        /* 5. Heartbeat Logic */
        uint32_t now = sys_tick_get_ms();
        if (now - last_heartbeat_ts > CLIENT_HEARTBEAT_MS) {
            int hb_res = uds_send_heartbeat_safe();
            /* 
             * If success (0) or sync error (-2), reset timer.
             * If busy (-1), do nothing (retry next loop).
             */
            if (hb_res == 0 || hb_res == -2) {
                last_heartbeat_ts = now;
            }
        }
    }
    
    shell_editor_stop(false);
    return exit_code;
}
