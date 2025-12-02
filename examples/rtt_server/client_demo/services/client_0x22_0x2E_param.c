/**
 * @file client_0x22_0x2E_param.c
 * @brief Services 0x22 (RDBI) & 0x2E (WDBI) Handler.
 * @details Implements client-side logic for Read Data By Identifier (0x22) and
 *          Write Data By Identifier (0x2E). Handles command parsing, request
 *          transmission, and formatted response printing (Hex + ASCII).
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
#define LOG_TAG "Param"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/response_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ==========================================================================
 * Static Response Handlers
 * ========================================================================== */

/**
 * @brief Handles the asynchronous response for Service 0x22 (SID 0x62).
 * @details Parses the positive response, extracts the DID, and prints the data
 *          payload in both Hexadecimal and ASCII formats.
 *
 * @param client Pointer to the UDS client instance.
 */
static void handle_rdbi_response(UDSClient_t *client) 
{
    /* 
     * Minimum Response Length: 3 bytes
     * [0] SID (0x62)
     * [1] DID High Byte
     * [2] DID Low Byte
     * [3...] Data Payload
     */
    if (client->recv_size < 3) {
        return;
    }

    uint16_t did = ((uint16_t)client->recv_buf[1] << 8) | client->recv_buf[2];
    int data_len = client->recv_size - 3;
    uint8_t *data = &client->recv_buf[3];

    /* Print header: [Param  ] DID 0x1234: */
    printf("\r[Param  ] DID 0x%04X: ", did);
    
    if (data_len == 0) {
        printf("(No Data)\n");
    } else {
        int i;
        /* Hex Dump */
        for (i = 0; i < data_len; i++) {
            printf("%02X ", data[i]);
        }
        
        /* Separator */
        printf("| ");
        
        /* ASCII Dump (Printable characters only) */
        for (i = 0; i < data_len; i++) {
            putchar(isprint(data[i]) ? data[i] : '.');
        }
        printf("\n");
    }
    
    /* Ensure output is displayed immediately */
    fflush(stdout);
}

/* ==========================================================================
 * CLI Command Handlers
 * ========================================================================== */

/**
 * @brief Handles the 'rdbi' (Read Data By Identifier) command.
 * @details Usage: rdbi <did_hex>
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, -1 on failure.
 */
static int handle_rdbi(int argc, char **argv) 
{
    uint16_t did;
    uint16_t did_list[1];

    if (argc < 2) {
        printf("Usage: rdbi <did_hex>\n");
        return 0;
    }

    /* Parse DID */
    did = (uint16_t)strtoul(argv[1], NULL, 16);
    did_list[0] = did;

    LOG_INFO("Reading DID: 0x%04X", did);

    /* Execute Transaction: Send 0x22 request for 1 DID */
    return UDS_TRANSACTION(UDSSendRDBI(uds_get_client(), did_list, 1), "Reading");
}

/**
 * @brief Handles the 'wdbi' (Write Data By Identifier) command.
 * @details Usage: wdbi <did_hex> <byte1> [byte2] ...
 *          Parses a variable number of hex byte arguments to form the payload.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, -1 on failure.
 */
static int handle_wdbi(int argc, char **argv) 
{
    uint16_t did;
    uint8_t buffer[128];
    int len = 0;
    int i;

    /* Require at least DID and 1 data byte */
    if (argc < 3) {
        printf("Usage: wdbi <did_hex> <data_hex...>\n");
        return 0;
    }

    did = (uint16_t)strtoul(argv[1], NULL, 16);

    /* Parse remaining arguments as hex data bytes */
    for (i = 2; i < argc && len < 128; i++) {
        buffer[len++] = (uint8_t)strtoul(argv[i], NULL, 16);
    }

    LOG_INFO("Writing DID: 0x%04X (%d bytes)", did, len);

    /* Execute Transaction: Send 0x2E request */
    return UDS_TRANSACTION(UDSSendWDBI(uds_get_client(), did, buffer, len), "Writing");
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initializes the Parameter Management services (0x22/0x2E).
 * @details Registers 'rdbi' and 'wdbi' commands and the 0x62 response handler.
 */
void client_0x22_0x2E_init(void) 
{
    /* Register RDBI Command */
    cmd_register("rdbi", handle_rdbi, "Read Data", " <did>");
    
    /* Register WDBI Command */
    cmd_register("wdbi", handle_wdbi, "Write Data", " <did> <data...>");
    
    /* Register Response Listener for 0x62 (RDBI Positive Response) */
    response_register(0x62, handle_rdbi_response);
}