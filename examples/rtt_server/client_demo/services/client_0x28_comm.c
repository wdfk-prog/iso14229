/**
 * @file client_0x28_comm.c
 * @brief Service 0x28 (Communication Control) Handler.
 * @details Implements the client-side logic for UDS Service 0x28. This service
 *          is used to switch on/off the transmission and reception of certain
 *          message groups (e.g., Application messages vs. Network Management messages)
 *          on the server.
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
#define LOG_TAG "Comm"

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
 * @brief Handles the 'cc' (Communication Control) shell command.
 * @details Usage: cc <ctrl> [comm] [node_id]
 *          - ctrl: Control Type (e.g., 0=EnableRxTx, 3=DisableRxTx).
 *          - comm: Communication Type (1=Normal, 2=NM, 3=Both).
 *          - node_id: Required only for Control Types 0x04 and 0x05 (Enhanced Address).
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, -1 on failure.
 */
static int handle_comm_ctrl(int argc, char **argv) 
{
    uint8_t ctrl;
    uint8_t comm = 0x03; /* Default: Normal + NM Messages */
    uint16_t node_id = 0;
    int use_node_id = 0;
    UDSErr_t err;

    /* Validate arguments */
    if (argc < 2) {
        printf("Usage: cc <ctrl> [comm] [id]\n");
        printf("  <ctrl>: 00=Enable, 01=DisTx, 03=Silent\n");
        printf("          04=DisTx(Enhanced), 05=Enable(Enhanced)\n");
        printf("  [comm]: 01=Norm, 02=NM, 03=Both (Default)\n");
        return 0;
    }

    /* Parse Control Type */
    ctrl = (uint8_t)strtoul(argv[1], NULL, 16);

    /* Parse optional Communication Type */
    if (argc > 2) {
        comm = (uint8_t)strtoul(argv[2], NULL, 16);
    }

    /* Parse optional Node ID */
    if (argc > 3) {
        node_id = (uint16_t)strtoul(argv[3], NULL, 16);
        use_node_id = 1;
    }

    /* 
     * Logic Check: ISO 14229-1 requires enhanced address information (Node ID)
     * for sub-functions 0x04 and 0x05.
     */
    if ((ctrl == 0x04 || ctrl == 0x05) && !use_node_id) {
        LOG_ERROR("Ctrl 0x%02X requires Node ID argument!", ctrl);
        return -1;
    }

    LOG_INFO("CommCtrl: Ctrl=0x%02X Comm=0x%02X ID=%s", 
             ctrl, comm, use_node_id ? argv[3] : "Global");

    /* 
     * Manual Transaction Preparation:
     * We manually call uds_prepare_request() here because we have conditional 
     * logic for the send function below, preventing the use of the single-line 
     * UDS_TRANSACTION macro.
     */
    uds_prepare_request();

    /* Select specific API based on addressing mode */
    if (use_node_id) {
        err = UDSSendCommCtrlWithNodeID(uds_get_client(), ctrl, comm, node_id);
    } else {
        err = UDSSendCommCtrl(uds_get_client(), ctrl, comm);
    }

    /* Wait for completion and validate response */
    if (uds_wait_transaction_result(err, "Requesting", 1000) == 0) {
        LOG_INFO("Success.");
        return 0;
    }

    return -1;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initializes the Communication Control service.
 * @details Registers the 'cc' command with the shell registry.
 */
void client_0x28_init(void) 
{
    /* 
     * Register command:
     * Name: "cc"
     * Handler: handle_comm_ctrl
     * Help: "Communication Control"
     * Hint: " <ctrl> [cm] [id]"
     */
    cmd_register("cc", handle_comm_ctrl, "Communication Control", " <ctrl> [cm] [id]");
}