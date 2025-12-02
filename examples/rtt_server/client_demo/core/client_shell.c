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
#include "../utils/linenoise.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

/* ==========================================================================
 * Configuration & Globals
 * ========================================================================== */

#define HISTORY_FILE            ".uds_history"
#define MAX_HEARTBEAT_RETRIES   3
#define POLL_INTERVAL_US        20000 /* 20ms polling interval */

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

/** @brief Current remote working directory for the prompt. */
static char g_remote_path[128] = "/";

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
    /* 1. Stop Line Editing (Restore terminal to cooked mode) */
    linenoiseEditStop(&g_ls);
    
    /* 2. Signal the main loop to break */
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
        /* Case B: Argument Completion (Files/Dirs) */
        const char *word_part = last_space + 1;
        size_t word_len = strlen(word_part);
        size_t prefix_len = word_part - buf;
        
        count = client_console_get_file_count();
        for (i = 0; i < count; i++) {
            const char *fname = client_console_get_file_name(i);
            
            /* Match against cached file list */
            if (fname && strncmp(word_part, fname, word_len) == 0) {
                char full_completion[256];
                if (prefix_len + strlen(fname) < sizeof(full_completion)) {
                    strncpy(full_completion, buf, prefix_len);
                    strcpy(full_completion + prefix_len, fname);
                    linenoiseAddCompletion(lc, full_completion);
                }
            }
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
    cmd_register("exit", NULL, "Exit Shell", "");

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

    /* Initial Prompt */
    snprintf(prompt, sizeof(prompt), "msh %s> ", g_remote_path);
    linenoiseEditStart(&g_ls, STDIN_FILENO, STDOUT_FILENO, buf, sizeof(buf), prompt);

    /* --- Event Loop --- */
    while (1) {
        /* 1. Check for Forced Exit (Heartbeat Failure) */
        if (g_shell_force_exit) {
            /* Note: linenoiseEditStop is called inside client_on_disconnect */
            printf("\r\n\033[1;31m[Fatal] Connection lost (Callback Triggered).\033[0m\r\n");
            exit_code = SHELL_EXIT_TIMEOUT;
            break;
        }

        /* 2. Prepare Select */
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        
        tv.tv_sec = 0;
        tv.tv_usec = POLL_INTERVAL_US; 

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        /* 3. Handle Input */
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            line = linenoiseEditFeed(&g_ls);
            
            if (line == linenoiseEditMore) {
                /* User is typing... */
            } 
            else if (line != NULL) {
                /* Complete line received */
                linenoiseEditStop(&g_ls); /* Restore terminal */
                
                if (strlen(line) > 0) {
                    linenoiseHistoryAdd(line);
                    linenoiseHistorySave(HISTORY_FILE);

                    /* Command Dispatch */
                    if (strcmp(line, "exit") == 0) {
                        free(line);
                        exit_code = SHELL_EXIT_USER;
                        break; 
                    } 
                    else if (strcmp(line, "help") == 0) {
                        cmd_execute_line(line); /* Local help handler */
                    }
                    else {
                        /* Execute command */
                        char *line_copy = strdup(line);
                        if (line_copy) {
                            /* Try local, fallback to remote */
                            int res = cmd_execute_line(line_copy);
                            if (res == -1) {
                                client_send_console_command(line); /* Send the original unmodified line*/
                            }
                            free(line_copy);
                        } else {
                            /* If memory allocation fails, still attempt to send the original command */
                            client_send_console_command(line);
                        }
                    }
                }
                
                free(line);
                
                /* Reset heartbeat timer on user activity */
                last_heartbeat_ts = sys_tick_get_ms();

                /* Re-enable Prompt */
                snprintf(prompt, sizeof(prompt), "msh %s> ", g_remote_path);
                linenoiseEditStart(&g_ls, STDIN_FILENO, STDOUT_FILENO, buf, sizeof(buf), prompt);
            } 
            else { 
                /* Error: Ctrl+C (EAGAIN) or Ctrl+D (ENOENT) */
                if (errno == EAGAIN || errno == ENOENT) {
                    linenoiseEditStop(&g_ls);
                    printf("\nQuit\n");
                    exit_code = SHELL_EXIT_USER;
                    break;
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
    
    linenoiseEditStop(&g_ls);
    return exit_code;
}