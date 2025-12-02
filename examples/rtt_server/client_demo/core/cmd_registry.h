/**
 * @file cmd_registry.h
 * @brief Command Registry Interface.
 * @details This header defines the public API for registering, looking up, 
 *          and executing local shell commands. It facilitates a decoupled 
 *          command pattern implementation.
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
#ifndef CMD_REGISTRY_H
#define CMD_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Type Definitions
 * ========================================================================== */

/**
 * @brief Function pointer prototype for command handlers.
 * @details Commands receive arguments in standard main-style (argc, argv).
 * 
 * @param argc Number of arguments (including command name).
 * @param argv Array of argument strings.
 * @return int 0 on success, non-zero error code on failure.
 */
typedef int (*cmd_handler_t)(int argc, char **argv);

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Initializes the command registry.
 * @details Resets the internal command table and counters. Should be called 
 *          before registering any commands.
 */
void cmd_registry_init(void);

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
int cmd_register(const char *name, cmd_handler_t handler, const char *help, const char *hint);

/**
 * @brief Parses and executes a command line.
 * @details Tokenizes the input string and searches for a matching command name.
 * 
 * @param input_line The raw input string (Note: may be modified by tokenizer).
 * @return int 0 on success, -1 if command is not found, or handler return code.
 */
int cmd_execute_line(char *input_line);

/**
 * @brief Prints the list of registered commands and their help text.
 * @details Outputs formatted text to stdout.
 */
void cmd_print_help(void);

/**
 * @brief Retrieves the total number of registered commands.
 * @return int The count of commands.
 */
int cmd_get_count(void);

/**
 * @brief Retrieves the command name at a specific index.
 * @details Useful for iteration or autocomplete logic.
 * 
 * @param index The index in the registry table.
 * @return const char* The command name, or NULL if index is invalid.
 */
const char* cmd_get_name(int index);

/**
 * @brief Retrieves the usage hint for a specific command name.
 * @details Hints provide parameter guidance (e.g., " <file>").
 * 
 * @param name The command name to look up.
 * @return const char* The hint string, or NULL if not found/set.
 */
const char* cmd_get_hint(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CMD_REGISTRY_H */