/**
 * @file cmd_registry.c
 * @brief Implementation of the Command Registry system.
 * @details Manages the registration, lookup, and execution of local shell commands.
 *          Implements a simple command pattern where commands are stored in a static array.
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
#include "cmd_registry.h"
#include "client_config.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ==========================================================================
 * Type Definitions
 * ========================================================================== */

/**
 * @brief Internal structure to represent a registered command.
 */
typedef struct {
    const char *name;       /**< Command keyword (e.g., "help"). */
    cmd_handler_t handler;  /**< Function pointer to the command handler. */
    const char *help;       /**< Short help description. */
    const char *hint;       /**< Parameter hint (e.g., "<arg1>"). Set to NULL in current API. */
} client_cmd_t;

/* ==========================================================================
 * Static Variables
 * ========================================================================== */

/** @brief Static storage for registered commands. */
static client_cmd_t g_cmd_table[MAX_COMMANDS];

/** @brief Current number of registered commands. */
static int g_cmd_count = 0;

/* ==========================================================================
 * Private Function Prototypes
 * ========================================================================== */

/**
 * @brief Tokenizes an input string into an argument vector (argc/argv).
 * @details Modifies the input string by inserting null terminators between tokens.
 *          This is a zero-copy implementation suitable for embedded systems.
 *
 * @param str      Input string (will be modified).
 * @param argv     Output array of string pointers.
 * @param max_args Maximum number of arguments `argv` can hold.
 * @return int Number of arguments found.
 */
static int split_args(char *str, char **argv, int max_args);

/* ==========================================================================
 * Public Function Implementation
 * ========================================================================== */

/**
 * @brief Initializes or resets the command registry.
 */
void cmd_registry_init(void) 
{
    g_cmd_count = 0;
    memset(g_cmd_table, 0, sizeof(g_cmd_table));
}

/**
 * @brief Registers a new command handler.
 * @note  The `hint` field is currently initialized to NULL as the function signature
 *        only accepts 3 arguments based on the provided implementation.
 * 
 * @param name    Command name string.
 * @param handler Function pointer to handle the command.
 * @param help    Help description string.
 * @param hint    Hint string.
 * @return int 0 on success, -1 on failure (table full or duplicate).
 */
int cmd_register(const char *name, cmd_handler_t handler, const char *help, const char *hint)
{
    int i;

    /* Check for table overflow */
    if (g_cmd_count >= MAX_COMMANDS) {
        return -1;
    }
    
    /* Validate input arguments */
    if (name == NULL || handler == NULL) {
        return -1;
    }

    /* Duplicate check */
    for (i = 0; i < g_cmd_count; i++) {
        if (strcmp(g_cmd_table[i].name, name) == 0) {
            return -1; /* Already registered */
        }
    }

    /* Register new command */
    g_cmd_table[g_cmd_count].name    = name;
    g_cmd_table[g_cmd_count].handler = handler;
    g_cmd_table[g_cmd_count].help    = help;
    g_cmd_table[g_cmd_count].hint    = hint;

    g_cmd_count++;
    return 0;
}

/**
 * @brief Retrieves the hint string for a given command.
 * @param name Command name.
 * @return const char* Pointer to hint string or NULL if not found/not set.
 */
const char* cmd_get_hint(const char *name) 
{
    int i;
    for (i = 0; i < g_cmd_count; i++) {
        if (strcmp(g_cmd_table[i].name, name) == 0) {
            return g_cmd_table[i].hint;
        }
    }
    return NULL;
}

/**
 * @brief Parses and executes a command line string.
 * 
 * @param input_line Raw command string (will be modified during parsing).
 * @return int Return value of the handler, or -1 if command not found.
 */
int cmd_execute_line(char *input_line) 
{
    char *argv[CMD_MAX_ARGS];
    int argc;
    int i;

    /* Sanity check */
    if (input_line == NULL || strlen(input_line) == 0) {
        return -1;
    }

    /* Tokenize input */
    argc = split_args(input_line, argv, CMD_MAX_ARGS);
    if (argc == 0) {
        return -1;
    }

    /* Lookup and execute */
    for (i = 0; i < g_cmd_count; i++) {
        if (strcmp(argv[0], g_cmd_table[i].name) == 0) {
            return g_cmd_table[i].handler(argc, argv);
        }
    }

    return -1; /* Command not found */
}

/**
 * @brief Returns the total number of registered commands.
 * @return int Command count.
 */
int cmd_get_count(void) 
{
    return g_cmd_count;
}

/**
 * @brief Returns the command name at a specific index.
 * @param index Index in the registry table.
 * @return const char* Command name or NULL if index is invalid.
 */
const char* cmd_get_name(int index) 
{
    if (index >= 0 && index < g_cmd_count) {
        return g_cmd_table[index].name;
    }
    return NULL;
}

/**
 * @brief Prints the list of all registered commands with help text.
 */
void cmd_print_help(void) 
{
    int i;
    printf("\n[Local Commands]\n");
    
    for (i = 0; i < g_cmd_count; i++) {
        /* Format: Name (10) | Hint (25) | Help Description */
        printf("  %-10s %-25s - %s\n", 
               g_cmd_table[i].name, 
               g_cmd_table[i].hint ? g_cmd_table[i].hint : "", 
               g_cmd_table[i].help ? g_cmd_table[i].help : "");
    }
    printf("\n");
}

/* ==========================================================================
 * Private Function Implementation
 * ========================================================================== */

static int split_args(char *str, char **argv, int max_args) 
{
    int argc = 0;
    char *p = str;

    while (*p && argc < max_args) {
        /* Skip leading whitespace */
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Store the start of the token */
        argv[argc++] = p;

        /* Find the end of the token (whitespace) */
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }

        /* Null-terminate the token if we haven't reached end of string */
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}