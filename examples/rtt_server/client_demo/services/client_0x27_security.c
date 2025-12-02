/**
 * @file client_0x27_security.c
 * @brief Service 0x27 (Security Access) Handler.
 * @details Implements the UDS Security Access logic, including the Seed & Key
 *          exchange mechanism. It handles the multi-stage transaction:
 *          1. Request Seed (Level N).
 *          2. Calculate Key using a specific algorithm.
 *          3. Send Key (Level N+1).
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
#define LOG_TAG "Sec"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 

/* ==========================================================================
 * Configuration & Constants
 * ========================================================================== */

/** 
 * @brief Default Secret Key Mask (XOR).
 * @note Must match the algorithm expected by the server (ECU).
 */
#define SECRET_KEY_MASK 0xA5A5A5A5 

/* ==========================================================================
 * Internal State
 * ========================================================================== */

/** @brief Storage for the seed received from the ECU. */
static uint8_t s_seed[4];

/** @brief Flag indicating a valid seed has been received and stored. */
static int s_seed_ready = 0;

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief Calculates the Security Key based on the received Seed.
 * @details This implementation uses a simple XOR algorithm.
 *          Replace this with the specific OEM algorithm (e.g., AES, SAJ1024)
 *          for production environments.
 *
 * @param seed The 4-byte seed received from the ECU.
 * @return uint32_t The calculated 4-byte key.
 */
static uint32_t calc_key(uint32_t seed) 
{
    return seed ^ SECRET_KEY_MASK;
}

/* ==========================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Performs the full Security Access sequence (Seed & Key).
 * @details This function is stateful and blocking. It executes two distinct
 *          UDS transactions in sequence.
 *
 * @param level The requested security level (must be an odd number, e.g., 0x01).
 * @return int 0 on success (Unlocked), -1 on failure.
 */
int client_perform_security(uint8_t level) 
{
    /* Get the singleton client instance */
    UDSClient_t *client = uds_get_client();
    UDSErr_t err;
    uint32_t seed_val;
    uint32_t key_val;
    uint8_t key_bytes[4];

    /* Validate Level: ISO 14229 requires 'RequestSeed' to be an odd number */
    if (level % 2 == 0) {
        LOG_ERROR("Invalid Security Level 0x%02X (Must be odd)", level);
        return -1;
    }

    LOG_INFO("Starting Security Access (Level 0x%02X)...", level);

    /* --- Step 1: Request Seed --- */
    s_seed_ready = 0;
    
    /* 
     * Transaction 1: Send 0x27 <Level> 
     * We manually call prepare/send/wait because we need to parse the payload 
     * between the request and the subsequent key send.
     */
    uds_prepare_request();
    err = UDSSendSecurityAccess(client, level, NULL, 0);
    
    if (uds_wait_transaction_result(err, "Requesting Seed", 2000) != 0) {
        return -1;
    }

    /* 
     * Parse Seed from Response Buffer.
     * Expected: [0x67] [Level] [S1] [S2] [S3] [S4]
     */
    if (client->recv_buf[0] == (0x40 + 0x27) && client->recv_buf[1] == level) {
        if (client->recv_size >= 6) {
            /* Standard case: Non-zero seed received */
            memcpy(s_seed, &client->recv_buf[2], 4);
            s_seed_ready = 1;
        } else if (client->recv_size >= 2) {
            /* 
             * Edge case: Server returns 0-length seed or all zeros 
             * to indicate the level is ALREADY unlocked.
             */
            LOG_INFO("Already Unlocked.");
            return 0;
        }
    }

    if (!s_seed_ready) {
        LOG_ERROR("Invalid Seed Response");
        return -1;
    }

    /* Combine bytes to integer (Big Endian) */
    seed_val = ((uint32_t)s_seed[0] << 24) | ((uint32_t)s_seed[1] << 16) |
               ((uint32_t)s_seed[2] << 8)  | (uint32_t)s_seed[3];

    /* --- Step 2: Calculate Key --- */
    key_val = calc_key(seed_val);
    
    /* Serialize Key (Big Endian) */
    key_bytes[0] = (uint8_t)((key_val >> 24) & 0xFF);
    key_bytes[1] = (uint8_t)((key_val >> 16) & 0xFF);
    key_bytes[2] = (uint8_t)((key_val >> 8) & 0xFF);
    key_bytes[3] = (uint8_t)(key_val & 0xFF);

    LOG_INFO("Seed: 0x%08X -> Key: 0x%08X", seed_val, key_val);

    /* --- Step 3: Send Key --- */
    /* Transaction 2: Send 0x27 <Level+1> <Key...> */
    uds_prepare_request();
    err = UDSSendSecurityAccess(client, level + 1, key_bytes, 4);
    
    if (uds_wait_transaction_result(err, "Verifying Key", 1000) != 0) {
        return -1;
    }

    LOG_INFO("Security Access Granted!");
    return 0;
}

/* ==========================================================================
 * CLI Command Handlers
 * ========================================================================== */

/**
 * @brief Handles the 'auth' shell command.
 * @details Usage: auth <level_hex>
 *          Defaults to Level 0x01 if no argument is provided.
 * 
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, -1 on failure.
 */
static int handle_auth_cmd(int argc, char **argv) 
{
    uint8_t level = 0x01; /* Default Level 1 */

    if (argc > 1) {
        level = (uint8_t)strtoul(argv[1], NULL, 16);
    } else {
        printf("Usage: auth <level_hex>\n");
        printf("  01 : Request Level 1 (Standard)\n");
        printf("  03 : Request Level 3 (Programming)\n");
        printf("Note: You must request the SEED level (odd number).\n");
        return 0;
    }

    return client_perform_security(level);
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initializes the Security Access service.
 * @details Registers the 'auth' command with the shell registry.
 */
void client_0x27_init(void) 
{
    /* 
     * Register command:
     * Name: "auth"
     * Handler: handle_auth_cmd
     * Help: "Security Access (0x27) - Unlock ECU"
     * Hint: " <level>" (Displayed in grey while typing)
     */
    cmd_register("auth", handle_auth_cmd, "Security Access (0x27) - Unlock ECU", " <level>");
}