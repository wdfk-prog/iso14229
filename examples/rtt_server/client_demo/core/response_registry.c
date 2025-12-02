/**
 * @file response_registry.c
 * @brief Implementation of the UDS Response Registry system.
 * @details Manages the registration and dispatching of handlers for specific UDS
 *          Service IDs (SIDs). This allows different service modules to subscribe
 *          to asynchronous responses (e.g., 0x71 RoutineControl response).
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
#include "response_registry.h"
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Configuration Macros
 * ========================================================================== */

/** @brief Maximum number of registered response handlers. */
#define MAX_HANDLERS 16

/* ==========================================================================
 * Type Definitions
 * ========================================================================== */

/**
 * @brief Internal structure for a response handler entry.
 */
typedef struct {
    uint8_t sid;                /**< Service ID (Response SID, e.g., 0x71). */
    uds_res_handler_t handler;  /**< Function pointer to the handler callback. */
} ResEntry;

/* ==========================================================================
 * Static Variables
 * ========================================================================== */

/** @brief Static storage for registered handlers. */
static ResEntry g_res_table[MAX_HANDLERS];

/** @brief Current count of registered handlers. */
static int g_res_count = 0;

/* ==========================================================================
 * Public Function Implementation
 * ========================================================================== */

/**
 * @brief Initializes or resets the response registry.
 * @details Clears the handler table and resets the counter.
 */
void response_registry_init(void) 
{
    g_res_count = 0;
    memset(g_res_table, 0, sizeof(g_res_table));
}

/**
 * @brief Registers a handler for a specific UDS Response SID.
 * @details If a handler for the given SID already exists, it will be overwritten.
 *          This allows dynamic re-registration of handlers if needed.
 *
 * @param sid     The Response Service ID (e.g., 0x71 for RoutineControl).
 * @param handler The callback function to execute when this SID is received.
 * @return int    0 on success, -1 if the registry table is full.
 */
int response_register(uint8_t sid, uds_res_handler_t handler) 
{
    int i;

    /* Check for duplicate/overwrite first */
    for (i = 0; i < g_res_count; i++) {
        if (g_res_table[i].sid == sid) {
            g_res_table[i].handler = handler; /* Overwrite existing handler */
            return 0;
        }
    }

    /* Check capacity */
    if (g_res_count >= MAX_HANDLERS) {
        return -1;
    }

    /* Add new entry */
    g_res_table[g_res_count].sid = sid;
    g_res_table[g_res_count].handler = handler;
    g_res_count++;

    return 0;
}

/**
 * @brief Dispatches a received UDS response to the appropriate handler.
 * @details Iterates through the registry to find a handler matching the
 *          response SID (first byte of recv_buf).
 *
 * @param client Pointer to the UDS Client instance containing received data.
 */
void response_dispatch(UDSClient_t *client) 
{
    int i;
    uint8_t sid;

    /* Safety checks */
    if (client == NULL || client->recv_size == 0) {
        return;
    }

    sid = client->recv_buf[0];

    for (i = 0; i < g_res_count; i++) {
        if (g_res_table[i].sid == sid) {
            if (g_res_table[i].handler != NULL) {
                g_res_table[i].handler(client);
            }
            return; /* Found and handled, stop searching */
        }
    }
}