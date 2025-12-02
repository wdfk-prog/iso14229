/**
 * @file client_0x11_reset.c
 * @brief Service 0x11 (ECU Reset) Handler.
 * @details Implements the client-side logic for requesting an ECU reset via
 *          UDS Service 0x11. It handles the request transmission and enforces
 *          a post-reset delay to allow the server time to reboot.
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
#define LOG_TAG "Reset"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>

/* ==========================================================================
 * CLI Command Handlers
 * ========================================================================== */

/**
 * @brief Handles the 'er' (ECU Reset) shell command.
 * @details Usage: er <type_hex>
 *          Sends a 0x11 request. If positive response is received, the client
 *          waits for a short period to accommodate the ECU's reboot sequence.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, -1 on failure.
 */
static int handle_reset(int argc, char **argv) 
{
    uint8_t reset_type;

    /* Check arguments */
    if (argc > 1) {
        /* Parse reset type (e.g., 01=Hard, 03=Soft) */
        reset_type = (uint8_t)strtoul(argv[1], NULL, 16);
    } else {
        /* Print usage if no type specified */
        printf("Usage: er <type_hex>\n");
        printf("  01: Hard Reset\n");
        printf("  02: Key Off/On\n");
        printf("  03: Soft Reset\n");
        return 0;
    }

    LOG_INFO("Sending ECU Reset (Type: 0x%02X)...", reset_type);

    /* 
     * Execute UDS Transaction:
     * 1. Get client instance via uds_get_client().
     * 2. Send Service 0x11 request.
     * 3. Wait for response ("Resetting ECU").
     */
    if (UDS_TRANSACTION(UDSSendECUReset(uds_get_client(), reset_type), "Resetting ECU") == 0) {
        LOG_INFO("Reset Accepted. ECU is rebooting...");
        
        /* 
         * Post-Reset Delay:
         * We delay execution here to allow the physical ECU time to process 
         * the reset and actually reboot. Sending commands immediately after 
         * this might result in timeouts or transport errors.
         */
        sys_delay_ms(1000); 
        return 0;
    }
    
    return -1;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initializes the ECU Reset service.
 * @details Registers the 'er' command with the shell registry.
 */
void client_0x11_init(void) 
{
    /* 
     * Register command:
     * Name: "er"
     * Handler: handle_reset
     * Help: "ECU Reset"
     * Hint: " <type>" (Displayed in grey while typing)
     */
    cmd_register("er", handle_reset, "ECU Reset", " <type>");
}