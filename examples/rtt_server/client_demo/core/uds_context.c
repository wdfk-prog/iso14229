/**
 * @file uds_context.c
 * @brief Implementation of the UDS Context with Encapsulated State.
 * @details This module manages the lifecycle of the UDS client instance,
 *          SocketCAN transport, and critical error monitoring (heartbeat failure).
 *          It hides internal state (Globals) and exposes a clean API for
 *          transactions and event polling.
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
#define LOG_TAG "Context"

#include "uds_context.h"
#include "response_registry.h"
#include "client_config.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ==========================================================================
 * Configuration Macros
 * ========================================================================== */

#define MAX_HEARTBEAT_RETRIES 3

/* ==========================================================================
 * Private Static Data (Encapsulated)
 * ========================================================================== */

/** @brief Internal UDS Client instance. */
static UDSClient_t g_client;

/** @brief Internal ISO-TP SocketCAN transport instance. */
static UDSTpIsoTpSock_t g_tp;

/** @brief Flag indicating a transaction (Send/Recv) has completed. */
static volatile bool g_response_received = false;

/** @brief Last captured Negative Response Code. */
static uint8_t g_last_nrc = 0;

/** @brief Counter for consecutive transport/heartbeat failures. */
static volatile int g_heartbeat_fail_count = 0;

/** @brief Registered callback for handling fatal disconnection events. */
static uds_disconnect_callback_t g_disconnect_cb = NULL;

/** @brief Original transport poll function pointer (saved for hooking). */
static UDSTpStatus_t (*original_tp_poll)(struct UDSTp *hdl) = NULL;

/* ==========================================================================
 * Internal Helpers & Hooks
 * ========================================================================== */

/**
 * @brief Triggers the user-registered disconnect callback.
 * @details Called when heartbeat failure count exceeds the threshold.
 */
static void trigger_disconnect_logic(void) 
{
    if (g_disconnect_cb) {
        g_disconnect_cb();
    }
}

/**
 * @brief Intercepts the transport layer's poll function.
 * @details Wraps the low-level `isotp_sock_tp_poll` to capture asynchronous 
 *          socket errors (e.g., ECOMM) that might otherwise be swallowed.
 *          Increments the failure counter and triggers disconnect logic if needed.
 * 
 * @param hdl Transport handle.
 * @return UDSTpStatus_t Status from the original poll function.
 */
static UDSTpStatus_t intercepted_tp_poll(struct UDSTp *hdl) 
{
    /* Call the real poll function */
    UDSTpStatus_t status = original_tp_poll(hdl);

    /* Check for transport errors (bit 2) */
    if (status & UDS_TP_ERR) {
        g_heartbeat_fail_count++;
        
        /* Immediate threshold check */
        if (g_heartbeat_fail_count >= MAX_HEARTBEAT_RETRIES) {
            trigger_disconnect_logic();
        }
    }
    return status;
}

/**
 * @brief Central UDS Library Event Handler.
 * @details Processes callbacks from the core library. Dispatches responses 
 *          to registered services and handles error reporting.
 */
static UDSErr_t client_event_handler(UDSClient_t *client, UDSEvent_t evt, void *ev_data) 
{
    (void)client;

    switch (evt) {
        case UDS_EVT_ResponseReceived:
            /* Dispatch to service listeners (e.g., 0x71 console handler) */
            response_dispatch(client);
            
            g_response_received = true;
            g_last_nrc = 0;
            g_heartbeat_fail_count = 0; /* Reset fail count on successful comms */
            break;

        case UDS_EVT_Err:
            if (ev_data) {
                UDSErr_t err = *(UDSErr_t *)ev_data;
                
                /* Extract NRC if applicable */
                g_last_nrc = ((err & 0xFF00) == 0) ? (uint8_t)err : 0xFF;
                
                /* Catch transport errors reported by the library logic */
                if (err == UDS_ERR_TPORT) {
                    g_heartbeat_fail_count++;
                    if (g_heartbeat_fail_count >= MAX_HEARTBEAT_RETRIES) {
                        trigger_disconnect_logic();
                    }
                }
            }
            g_response_received = true; /* Unblock waiting threads */
            break;

        default:
            break;
    }
    return UDS_OK;
}

/* ==========================================================================
 * Public API Implementation - Accessors
 * ========================================================================== */

UDSClient_t* uds_get_client(void) 
{
    return &g_client;
}

uint8_t uds_get_last_nrc(void) 
{
    return g_last_nrc;
}

void uds_register_disconnect_callback(uds_disconnect_callback_t cb) 
{
    g_disconnect_cb = cb;
}

void uds_poll(void) 
{
    UDSClientPoll(&g_client);
}

/* ==========================================================================
 * Public API Implementation - Lifecycle
 * ========================================================================== */

int uds_context_init(void) 
{
    UDSErr_t err;

    /* 1. Reset state */
    memset(&g_tp, 0, sizeof(g_tp));
    g_tp.phys_fd = -1;
    g_tp.func_fd = -1;
    memset(&g_client, 0, sizeof(g_client));
    g_heartbeat_fail_count = 0;

    /* 2. Initialize Transport (SocketCAN) */
    err = UDSTpIsoTpSockInitClient(&g_tp, 
                                   g_uds_cfg.if_name, 
                                   g_uds_cfg.phys_sa, 
                                   g_uds_cfg.phys_ta, 
                                   g_uds_cfg.func_sa);
    if (err != UDS_OK) {
        LOG_ERROR("Failed to init SocketCAN on %s", g_uds_cfg.if_name);
        return -1;
    }

    /* 3. Install Hook */
    original_tp_poll = g_tp.hdl.poll;
    g_tp.hdl.poll = intercepted_tp_poll;

    /* 4. Initialize Client */
    err = UDSClientInit(&g_client);
    if (err != UDS_OK) {
        return -1;
    }

    /* 5. Link Dependencies */
    g_client.tp = (UDSTp_t *)&g_tp;
    g_client.fn = client_event_handler;

    LOG_INFO("UDS Context Initialized (IF: %s)", g_uds_cfg.if_name);
    return 0;
}

void uds_context_deinit(void) 
{
    if (g_tp.phys_fd >= 0) {
        UDSTpIsoTpSockDeinit(&g_tp);
    }
    
    if (g_tp.phys_fd >= 0) { 
        close(g_tp.phys_fd); 
        g_tp.phys_fd = -1; 
    }
    if (g_tp.func_fd >= 0) { 
        close(g_tp.func_fd); 
        g_tp.func_fd = -1; 
    }
    
    LOG_INFO("UDS Context Deinitialized");
}

/* ==========================================================================
 * Public API Implementation - Transaction Logic
 * ========================================================================== */

void uds_prepare_request(void) 
{
    g_response_received = false;
    g_last_nrc = 0;
}

int uds_wait_transaction_result(UDSErr_t send_err, const char *msg, uint32_t timeout_ms) 
{
    uint32_t start_time = sys_tick_get_ms();
    const char spinner[] = "|/-\\";
    int spin_idx = 0;
    int elapsed_loops = 0;

    /* 1. Check synchronous send error */
    if (send_err != UDS_OK) {
        LOG_ERROR("Send failed: %d", send_err);
        return -1;
    }

    /* 2. Wait for async response with spinner */
    if (msg) { 
        printf("%s...", msg); 
        fflush(stdout); 
    }

    while (!g_response_received) {
        uds_poll();

        /* Check Timeout */
        if (timeout_ms > 0 && (sys_tick_get_ms() - start_time > timeout_ms)) {
            if (msg) printf("\n");
            LOG_WARN("Timeout!");
            return -1;
        }

        /* Update Spinner (every 100ms) */
        if ((elapsed_loops++ % 100) == 0) {
            if (msg) {
                printf("\r[%c] ...", spinner[spin_idx]);
                fflush(stdout);
                spin_idx = (spin_idx + 1) % 4;
            }
        }
        sys_delay_ms(1);
    }

    if (msg) printf("\r[+] %s Done.   \n", msg);

    /* 3. Check for Protocol Errors (NRC) */
    if (g_last_nrc != 0) {
        LOG_ERROR("Operation Failed. NRC: 0x%02X", g_last_nrc);
        return -1;
    }

    return 0;
}

int uds_send_heartbeat_safe(void) 
{
    /* Ensure client is idle before sending heartbeat */
    if (g_client.state != 0) {
        return -1; 
    }

    uint8_t old_options = g_client.options;
    g_client.options |= UDS_SUPPRESS_POS_RESP;
    
    UDSErr_t err = UDSSendTesterPresent(&g_client);
    
    g_client.options = old_options;

    /* Check for synchronous send errors */
    if (err != UDS_OK) {
        g_heartbeat_fail_count++;
        
        if (g_heartbeat_fail_count >= MAX_HEARTBEAT_RETRIES) {
            trigger_disconnect_logic();
        }
        return -2;
    }
    return 0;
}