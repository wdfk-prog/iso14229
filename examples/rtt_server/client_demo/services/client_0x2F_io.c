/**
 * @file client_0x2F_io.c
 * @brief Service 0x2F (InputOutputControlByIdentifier) Handler.
 * @details Implements the client-side logic for UDS Service 0x2F, allowing
 *          control of ECU input/output signals (e.g., freezing, resetting,
 *          or short-term adjustment of values).
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
#define LOG_TAG "IO"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/response_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>

/* ==========================================================================
 * Static Function Implementations
 * ========================================================================== */

/**
 * @brief Handles the asynchronous response for Service 0x2F (SID 0x6F).
 * @details Parses the positive response from the ECU, which typically contains
 *          the Data Identifier (DID), the Control Parameter used, and the
 *          current state of the signals (optional).
 *
 * @param client Pointer to the UDS client instance containing the receive buffer.
 */
static void handle_io_response(UDSClient_t *client) 
{
    /* 
     * Minimum Response Length: 4 bytes
     * [0] SID (0x6F)
     * [1] DID High Byte
     * [2] DID Low Byte
     * [3] InputOutputControlParameter
     * [4...] ControlState (Optional)
     */
    if (client->recv_size < 4) {
        return;
    }

    /* Extract Data Identifier (Big Endian) */
    uint16_t did = ((uint16_t)client->recv_buf[1] << 8) | client->recv_buf[2];
    uint8_t param = client->recv_buf[3];
    int state_len = client->recv_size - 4;

    /* Print structured output */
    printf("\r[IO     ] DID 0x%04X Param 0x%02X State: ", did, param);
    
    if (state_len > 0) {
        /* Hex dump of the control state returned by ECU */
        for (int i = 0; i < state_len; i++) {
            printf("%02X ", client->recv_buf[4 + i]);
        }
    } else {
        printf("(No State)");
    }
    printf("\n");
    
    /* Ensure output is visible immediately (for raw mode shells) */
    fflush(stdout);
}

/**
 * @brief Command handler for 'io'.
 * @details Parses command line arguments to construct a 0x2F request.
 *          Usage: io <did> <param> [data...]
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success (transaction completed), -1 on error.
 */
static int handle_io(int argc, char **argv) 
{
    /* Validate minimum arguments: cmd, did, param */
    if (argc < 3) {
        printf("Usage: io <did_hex> <param_hex> [data...]\n");
        printf("  Params: 00=Return, 01=Reset, 02=Freeze, 03=ShortTerm\n");
        return 0;
    }

    /* Parse DID and Control Parameter */
    uint16_t did = (uint16_t)strtoul(argv[1], NULL, 16);
    uint8_t param = (uint8_t)strtoul(argv[2], NULL, 16);
    
    /* Buffer for ControlOptionRecord (Mask + State) */
    uint8_t buffer[32];
    int len = 0;

    /* 
     * Parse optional data bytes (starting from argv[3]).
     * These bytes represent the 'controlState' and 'controlMask' 
     * required for ShortTermAdjustment (0x03).
     */
    for (int i = 3; i < argc && len < 32; i++) {
        buffer[len++] = (uint8_t)strtoul(argv[i], NULL, 16);
    }

    LOG_INFO("IO Ctrl: DID=0x%04X Param=0x%02X", did, param);

    /* 
     * Execute UDS Transaction:
     * 1. Get Client Instance.
     * 2. Send Request.
     * 3. Wait for response with spinner animation.
     */
    return UDS_TRANSACTION(UDSSendIOControl(uds_get_client(), did, param, buffer, len), "Controlling IO");
}

/* ==========================================================================
 * Public Initialization
 * ========================================================================== */

/**
 * @brief Initializes the IO Control service.
 * @details Registers the CLI command 'io' and the UDS response handler for 0x6F.
 */
void client_0x2F_init(void) 
{
    /* Register command with Help and Hint separated */
    cmd_register("io", handle_io, "IO Control", " <did> <pm> [data]");
    
    /* Register observer for positive response (0x6F) */
    response_register(0x6F, handle_io_response);
}