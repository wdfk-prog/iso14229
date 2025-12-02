/**
 * @file main.c
 * @brief UDS Client Application Entry Point.
 * @details This file implements the main application lifecycle, including:
 *          - Command line argument parsing.
 *          - Service registration (Command Pattern).
 *          - Robust connection management with auto-reconnection logic.
 *          - Interactive shell execution.
 *
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
#define LOG_TAG "Main"

#include "core/client_config.h"
#include "core/uds_context.h"
#include "core/cmd_registry.h"
#include "core/client_shell.h"
#include "core/client.h"
#include "utils/utils.h"
#include <stdio.h>
#include <unistd.h>

/**
 * @brief Prompts the user to attempt a reconnection after a failure.
 * @details This function handles standard input flushing to ensure clean
 *          character reading, preventing newline artifacts from previous
 *          inputs from automatically validating the prompt.
 *
 * @return 1 (true) if the user confirms 'y' or 'Y', 0 (false) otherwise.
 */
static int ask_to_reconnect(void)
{
    int ch;
    char c;

    /* Flush stdout to ensure the prompt appears before blocking on input */
    printf("\r\nConnection lost or failed. Attempt to reconnect? (y/n): ");
    fflush(stdout);

    c = getchar();

    /* 
     * Input Buffer Clearing:
     * Consume the rest of the line (including the newline character) 
     * to prevent subsequent getchar() calls from reading immediate junk.
     */
    while ((ch = getchar()) != '\n' && ch != EOF);

    return (c == 'y' || c == 'Y');
}

/**
 * @brief Main application entry point.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return int Application exit status (0 for success).
 */
int main(int argc, char **argv)
{
    int shell_exit_code;
    int should_restart = 0;
    int retries;
    int connected;

    /* --- 1. Initial Configuration Phase --- */
    
    printf("\n========================================\n");
    printf("   UDS Client\n");
    printf("========================================\n");

    /* Parse CLI args to override default CAN interface/IDs before anything else */
    client_config_parse_args(argc, argv);

    /* --- 2. Service Registration Phase --- */
    
    /* Initialize the Command Registry (Command Pattern Container) */
    cmd_registry_init();

    /* Register specific UDS service handlers to the registry */
    client_0x10_init();         /* Diagnostic Session Control */
    client_0x27_init();         /* Security Access */
    client_0x2F_init();         /* IO Control */
    client_0x31_init();         /* Routine Control (Console) */
    client_file_svc_init();     /* File Transfer (0x34/35/36/37/38) */
    client_0x28_init();         /* Communication Control */
    client_0x11_init();         /* ECU Reset */

    /* --- 3. Main Application Loop (Reconnection Logic) --- */
    do {
        should_restart = 0;
        connected = 0;
        retries = 3;

        /* A. Initialize UDS Context (Transport Layer & Protocol Stack) */
        if (uds_context_init() != 0) {
            LOG_ERROR("Context Init Failed.");
            
            /* If hardware init fails (e.g., CAN interface down), ask to retry */
            if (ask_to_reconnect()) {
                should_restart = 1;
                continue; /* Restart the do-while loop */
            } else {
                break; /* Exit application */
            }
        }

        LOG_INFO("Auto-Connecting to ECU (0x%X)...", g_uds_cfg.phys_ta);

        /* B. Connection Sequence */
        while (retries--) {
            /* 
             * Attempt to switch to Extended Session (0x03).
             * This acts as a "Ping" and prepares the ECU for privileged operations.
             */
            if (client_request_session(0x03) == 0) {
                connected = 1;
                break;
            }
            LOG_WARN("Retrying connection (%d left)...", retries);
            sys_delay_ms(500);
        }

        if (!connected) {
            LOG_WARN("Connection Failed. Entering Offline Mode.");
            /* We continue to the shell even if offline, allowing local commands */
        } else {
            LOG_INFO("Connected! Security Access...");
            
            /* Attempt auto-unlock level 1 */
            if (client_perform_security(0x01) == 0) {
                LOG_INFO("Security Unlocked.");
            }
            
            /* 
             * Dynamic Discovery:
             * Fetch the available commands from the remote server via 0x31 RoutineControl.
             * This populates the autocomplete cache.
             */
            client_sync_remote_commands();
        }

        /* C. Interactive Shell Execution */
        client_shell_init();
        
        /* 
         * BLOCKING CALL: 
         * This function takes over control. It returns only when:
         * 1. User types 'exit' (SHELL_EXIT_USER)
         * 2. Heartbeat fails 3 times (SHELL_EXIT_TIMEOUT)
         */
        shell_exit_code = client_shell_loop();

        /* D. Cleanup Context */
        uds_context_deinit();

        /* E. Post-Mortem Analysis */
        if (shell_exit_code == SHELL_EXIT_TIMEOUT) {
            /* If the shell exited due to connection loss, verify if user wants to reconnect */
            if (ask_to_reconnect()) {
                should_restart = 1;
            }
        }
        /* If SHELL_EXIT_USER, should_restart remains 0, loop terminates */

    } while (should_restart);

    LOG_INFO("Exiting application.");
    return 0;
}