/**
 * @file client_0x10_session.c
 * @brief Service 0x10 (Diagnostic Session Control) Handler.
 * @details Implements the client-side logic for switching diagnostic sessions
 *          (Default, Programming, Extended) via UDS Service 0x10.
 *          Manages the transition state and verifies positive responses.
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
#define LOG_TAG "Session"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>

/* ==========================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Sends a diagnostic session control request (0x10).
 * @details This function wraps the UDS request in a transaction macro that
 *          handles state preparation, sending, spinner animation, and
 *          error checking (NRC validation).
 *
 * @param session_type The target session ID (e.g., 0x01 Default, 0x03 Extended).
 * @return int 0 on success (Positive Response received), -1 on failure.
 */
int client_request_session(uint8_t session_type) 
{
    LOG_INFO("Requesting Session Control: 0x%02X", session_type);

    /*
     * Execute UDS Transaction:
     * 1. uds_prepare_request(): Clears flags.
     * 2. UDSSendDiagSessCtrl(): Sends the ISO-TP frame.
     * 3. uds_wait_transaction_result(): Blocks with a spinner until timeout or response.
     */
    if (UDS_TRANSACTION(UDSSendDiagSessCtrl(uds_get_client(), session_type), "Switching Session") == 0) {
        LOG_INFO("Session Switched Successfully (0x%02X)", session_type);
        return 0;
    }

    return -1;
}

/* ==========================================================================
 * CLI Command Handlers
 * ========================================================================== */

/**
 * @brief Handles the 'session' shell command.
 * @details Usage: session <type_hex>
 *          Validates input range against ISO 14229-1 standard definitions.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success/handled, -1 on error.
 */
static int handle_session_cmd(int argc, char **argv) 
{
    uint8_t session_type = 0x01; 
    unsigned long val;

    /* Check if an argument was provided */
    if (argc > 1) {
        /* Parse input as Hexadecimal */
        val = strtoul(argv[1], NULL, 16);
        
        /* 
         * Range Check:
         * ISO 14229-1 defines SessionType as 1 byte (0x00-0xFF).
         * - 0x00 is reserved.
         * - 0x01-0x7F are standard/OEM specific sessions.
         * - Bit 7 (0x80) is the SuppressPositiveResponse bit, which should not
         *   be set manually here as the library handles it via options.
         */
        if (val == 0x00 || val > 0x7F) {
            printf("[!] Error: Invalid Session Type 0x%02lX. Valid range: 0x01 - 0x7F\n", val);
            return 0;
        }
        
        session_type = (uint8_t)val;
    } else {
        /* Print Help / Usage Guide */
        printf("Usage: session <type_hex>\n");
        printf("Description: Request ECU to switch diagnostic session.\n");
        printf("Standard Types:\n");
        printf("  01 : Default Session (Standard)\n");
        printf("  02 : Programming Session (Bootloader/Flashing)\n");
        printf("  03 : Extended Diagnostic Session (Unlock capabilities)\n");
        return 0;
    }

    /* Execute the logic via the public API */
    return client_request_session(session_type);
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initializes the Session Control service.
 * @details Registers the 'session' command with the shell registry.
 */
void client_0x10_init(void) 
{
    /* 
     * Register command:
     * Name: "session"
     * Handler: handle_session_cmd
     * Help: "Diagnostic Session Control (0x10)"
     * Hint: " <hex_type>" (Displayed in grey while typing)
     */
    cmd_register("session", handle_session_cmd, "Diagnostic Session Control (0x10)", " <hex_type>");
}