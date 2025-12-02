/**
 * @file client_0x31_console.c
 * @brief Service 0x31 (Routine Control) Handler for Remote Console.
 * @details Implements the logic for executing remote shell commands via UDS 0x31.
 *          Includes features for:
 *          - Command execution (rexec).
 *          - Directory navigation with path tracking (cd).
 *          - Remote command/file list caching and autocomplete support.
 *          - Silent synchronization mode to populate caches on startup.
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
#define LOG_TAG "RCon"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/response_registry.h"
#include "../core/uds_context.h"
#include "../core/client_shell.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#ifndef RID_REMOTE_CONSOLE
#define RID_REMOTE_CONSOLE 0xF000
#endif

#define MAX_CACHE_ITEMS 128

/* ==========================================================================
 * Static State
 * ========================================================================== */

/* Cache for remote commands (from 'help') */
static char *g_cmd_cache[MAX_CACHE_ITEMS];
static int g_cmd_count = 0;

/* Cache for remote files (from 'ls') */
static char *g_file_cache[MAX_CACHE_ITEMS];
static int g_file_count = 0;

/* Stores the last sent command string to determine how to parse the response */
static char g_last_sent_cmd[256] = {0};

/* Flags */
static int g_expecting_help = 0; /* 1 if we expect 'help' output next */
static int g_silent_mode = 0;    /* 1 if we should suppress console output (syncing) */

/* ==========================================================================
 * Cache Helpers
 * ========================================================================== */

static void clear_cmd_cache(void) 
{
    int i;
    for (i = 0; i < g_cmd_count; i++) {
        if (g_cmd_cache[i]) free(g_cmd_cache[i]);
    }
    g_cmd_count = 0;
}

static void clear_file_cache(void) 
{
    int i;
    for (i = 0; i < g_file_count; i++) {
        if (g_file_cache[i]) free(g_file_cache[i]);
    }
    g_file_count = 0;
}

static void add_to_file_cache(const char *name) 
{
    int i;
    if (g_file_count >= MAX_CACHE_ITEMS) return;
    
    /* Deduplicate */
    for (i = 0; i < g_file_count; i++) {
        if (strcmp(g_file_cache[i], name) == 0) return;
    }
    g_file_cache[g_file_count++] = strdup(name);
}

/* --- Public Getters (Accessed by Shell for Autocomplete) --- */

int client_console_get_cmd_count(void) 
{
    return g_cmd_count;
}

const char* client_console_get_cmd_name(int index) 
{
    return (index >= 0 && index < g_cmd_count) ? g_cmd_cache[index] : NULL;
}

int client_console_get_file_count(void) 
{
    return g_file_count;
}

const char* client_console_get_file_name(int index) 
{
    return (index >= 0 && index < g_file_count) ? g_file_cache[index] : NULL;
}

/* ==========================================================================
 * Output Parsers
 * ========================================================================== */

/**
 * @brief Parses the output of the 'help' command to populate the command cache.
 */
static void parse_help_output(char *text) 
{
    char *line;
    char *end;
    size_t len;

    clear_cmd_cache();
    
    line = strtok(text, "\r\n");
    while (line != NULL && g_cmd_count < MAX_CACHE_ITEMS) {
        /* Skip leading whitespace */
        while (*line && isspace((unsigned char)*line)) line++;
        
        if (*line != '\0') {
            /* Find end of the first word (command name) */
            end = line;
            while (*end && !isspace((unsigned char)*end)) end++;
            
            len = end - line;
            
            /* Filter out common shell headers (e.g., msh, RT-Thread) */
            if (len > 0 && strncmp(line, "msh", 3) != 0 && strncmp(line, "RT-Thread", 9) != 0) {
                g_cmd_cache[g_cmd_count] = malloc(len + 1);
                if (g_cmd_cache[g_cmd_count]) {
                    strncpy(g_cmd_cache[g_cmd_count], line, len);
                    g_cmd_cache[g_cmd_count][len] = '\0';
                    g_cmd_count++;
                }
            }
        }
        line = strtok(NULL, "\r\n");
    }
}

/**
 * @brief Parses the output of the 'ls' command to populate file cache and detect path.
 */
static void parse_ls_output(char *payload) 
{
    char *work_buf;
    char *line;
    char *p;
    char name[64];
    int i;
    int is_dir;

    /* Only parse if we actually sent 'ls' */
    if (strncmp(g_last_sent_cmd, "ls", 2) != 0) return;

    /* Duplicate payload because strtok modifies the string */
    work_buf = strdup(payload);
    if (!work_buf) return;

    clear_file_cache();

    line = strtok(work_buf, "\n");
    while (line) {
        /* Trim CR/LF */
        while (*line == '\r' || *line == '\n') line++;
        
        if (*line == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }

        /* 1. Detect Path Change: "Directory /flash:" */
        if (strncmp(line, "Directory", 9) == 0) {
            char *path_start = strchr(line, '/');
            if (path_start) {
                char *path_end = path_start;
                /* Find end of path */
                while (*path_end != ':' && *path_end != '\r' && *path_end != '\0') path_end++;
                
                if (*path_end == ':' || *path_end == '\r') {
                    *path_end = '\0';
                    client_shell_set_path(path_start); /* Update Shell Prompt */
                }
            }
            line = strtok(NULL, "\n");
            continue;
        }

        /* 2. Parse Filename */
        i = 0;
        p = line;
        /* Extract first word */
        while (*p && !isspace((unsigned char)*p) && i < 63) {
            name[i++] = *p++;
        }
        name[i] = '\0';

        is_dir = (strstr(line, "<DIR>") != NULL);
        
        if (strlen(name) > 0) {
            if (is_dir) {
                char dir_name[70];
                snprintf(dir_name, sizeof(dir_name), "%s/", name);
                add_to_file_cache(dir_name);
            } else {
                add_to_file_cache(name);
            }
        }
        line = strtok(NULL, "\n");
    }
    free(work_buf);
}

/* ==========================================================================
 * Response Handler
 * ========================================================================== */

/**
 * @brief Handles 0x71 (RoutineControl Response) for Console Output.
 */
static void handle_console_response(UDSClient_t *client) 
{
    uint16_t rid;
    const char *payload;
    int len;
    int i;
    char *buf_copy;

    if (client->recv_size <= 4) return;

    rid = ((uint16_t)client->recv_buf[2] << 8) | client->recv_buf[3];
    if (rid != RID_REMOTE_CONSOLE) return;

    payload = (const char *)&client->recv_buf[4];
    len = client->recv_size - 4;

    /* 1. Print Output (if not silent) */
    if (!g_silent_mode) {
        for (i = 0; i < len; i++) {
            /* Fix raw mode newline behavior */
            if (payload[i] == '\n') putchar('\r');
            putchar(payload[i]);
        }
        fflush(stdout);
    }

    /* 2. Parse Output for Cache/State Updates */
    buf_copy = malloc(len + 1);
    if (buf_copy) {
        memcpy(buf_copy, payload, len);
        buf_copy[len] = '\0';
        
        if (g_expecting_help) {
            parse_help_output(buf_copy);
            /* Keep expecting_help set until sync function clears it or new command sent */
        } 
        else if (strncmp(g_last_sent_cmd, "ls", 2) == 0) {
            parse_ls_output(buf_copy);
        }
        free(buf_copy);
    }
}

/* ==========================================================================
 * Sending Logic
 * ========================================================================== */

int client_send_console_command(const char *cmd_str) 
{
    UDSClient_t *client = uds_get_client();
    int retry = 10;
    size_t len;
    const char *p;
    UDSErr_t err;
    const char *wait_msg;

    /* Wait for client IDLE state to avoid UDS_ERR_BUSY */
    while (client->state != 0 && retry-- > 0) { 
        uds_poll();
        sys_delay_ms(10);
    }
    
    if (client->state != 0) {
        LOG_WARN("Client Busy, cannot send '%s'", cmd_str);
        return -1;
    }

    len = strlen(cmd_str);
    if (len == 0) return 0;

    if (!g_silent_mode) {
        LOG_INFO("Remote Exec: '%s'", cmd_str);
    }
    
    /* Update state tracking */
    strncpy(g_last_sent_cmd, cmd_str, sizeof(g_last_sent_cmd) - 1);
    
    /* Check if command is 'help' to enable parser */
    p = cmd_str;
    while (*p && isspace((unsigned char)*p)) p++;
    if (strncmp(p, "help", 4) == 0) {
        g_expecting_help = 1;
    } else {
        g_expecting_help = 0;
    }

    /* Prepare & Send */
    uds_prepare_request();
    err = UDSSendRoutineCtrl(client, 0x01, RID_REMOTE_CONSOLE, (uint8_t*)cmd_str, len);
    
    if (err != UDS_OK) {
        LOG_ERROR("Send failed: %d", err);
        return -1;
    }

    /* 
     * Wait for response.
     * If silent mode is active, pass NULL to suppress the spinner animation.
     * Increased timeout (8000ms) for potentially long console commands.
     */
    wait_msg = g_silent_mode ? NULL : NULL; 
    
    if (uds_wait_transaction_result(UDS_OK, wait_msg, 8000) == 0) {
        return 0;
    }
    return -1; 
}

/* ==========================================================================
 * CLI Handlers
 * ========================================================================== */

static void resolve_path(char *target, const char *base, const char *append) 
{
    if (append[0] == '/') {
        /* Absolute path */
        strcpy(target, append);
    } else if (strcmp(append, "..") == 0) {
        /* Parent dir */
        strcpy(target, base);
        char *last_slash = strrchr(target, '/');
        if (last_slash && last_slash != target) {
            *last_slash = '\0';
        } else {
            strcpy(target, "/");
        }
    } else {
        /* Relative path */
        strcpy(target, base);
        size_t len = strlen(target);
        if (len > 0 && target[len-1] != '/') strcat(target, "/");
        strcat(target, append);
    }
}

static int handle_cd(int argc, char **argv) 
{
    char cmd_buf[256];
    char new_path_guess[256];
    
    if (argc > 1) {
        snprintf(cmd_buf, sizeof(cmd_buf), "cd %s", argv[1]);
        
        /* Optimistic Local Update: Update prompt immediately */
        resolve_path(new_path_guess, client_shell_get_path(), argv[1]);
        client_shell_set_path(new_path_guess);
    } else {
        snprintf(cmd_buf, sizeof(cmd_buf), "cd /");
        client_shell_set_path("/");
    }
    
    return client_send_console_command(cmd_buf);
}

static int handle_rexec(int argc, char **argv) 
{
    int i;
    char full_cmd[256] = {0};

    if (argc < 2) return 0;
    
    for (i = 1; i < argc; i++) {
        strncat(full_cmd, argv[i], sizeof(full_cmd) - strlen(full_cmd) - 1);
        if (i < argc - 1) strncat(full_cmd, " ", sizeof(full_cmd) - strlen(full_cmd) - 1);
    }
    return client_send_console_command(full_cmd);
}

void client_0x31_init(void) 
{
    /* Register Commands */
    cmd_register("rexec", handle_rexec, "Explicit Remote Exec", " <cmd>");
    cmd_register("cd", handle_cd, "Change Remote Dir", " <path>");
    
    /* Register Response Listener */
    response_register(0x71, handle_console_response);
}