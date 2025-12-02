/**
 * @file uds_context.h
 * @brief UDS Context, State Management, and Transaction Helpers.
 * @details This header provides an encapsulated interface for the UDS client instance.
 *          It defines standardized mechanisms for executing diagnostic transactions,
 *          managing connection lifecycle, and handling heartbeats.
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
#ifndef UDS_CONTEXT_H
#define UDS_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Include the core ISO-14229 library definitions */
#include "../iso14229.h"
#include <stdint.h>
#include <stdbool.h>

/* ==========================================================================
 * Type Definitions
 * ========================================================================== */

/**
 * @brief Callback function type for connection loss events.
 * @details This callback is invoked by the context manager when the heartbeat
 *          fails consecutively (e.g., link loss or ECU reset). The application/shell
 *          should register this to handle cleanup and reconnection logic.
 */
typedef void (*uds_disconnect_callback_t)(void);

/* ==========================================================================
 * Public API - Instance Access
 * ========================================================================== */

/**
 * @brief Retrieves the singleton UDS Client instance.
 * @details Provides access to the internal `UDSClient_t` structure for operations
 *          that require direct client manipulation.
 *
 * @return UDSClient_t* Pointer to the global UDS Client instance.
 */
UDSClient_t* uds_get_client(void);

/**
 * @brief Retrieves the last received Negative Response Code (NRC).
 * @details This value is updated whenever a request fails with an NRC.
 *          It is reset to 0x00 upon a successful transaction.
 *
 * @return uint8_t 0x00 if the last operation was successful, otherwise the NRC value.
 */
uint8_t uds_get_last_nrc(void);

/* ==========================================================================
 * Public API - Lifecycle Management
 * ========================================================================== */

/**
 * @brief Initializes the UDS Context.
 * @details Sets up the SocketCAN interface, initializes the ISO-TP transport layer,
 *          and configures the UDS client. Must be called before any other API.
 *
 * @return int 0 on success, -1 on failure (e.g., socket creation error).
 */
int uds_context_init(void);

/**
 * @brief Cleans up UDS Context resources.
 * @details Closes open sockets, releases file descriptors, and resets internal state.
 */
void uds_context_deinit(void);

/**
 * @brief Registers a callback for disconnection events.
 * @param cb The function pointer to call when the connection is considered lost.
 */
void uds_register_disconnect_callback(uds_disconnect_callback_t cb);

/* ==========================================================================
 * Public API - Transaction Helpers
 * ========================================================================== */

/**
 * @brief Prepares the context for a new request.
 * @details Resets internal flags (e.g., response_received) and clears the last NRC.
 *          @note This is typically called automatically by the `UDS_TRANSACTION` macro.
 */
void uds_prepare_request(void);

/**
 * @brief Waits for a transaction to complete.
 * @details This is a blocking call that polls the UDS stack until a response is received
 *          or a timeout occurs. It handles displaying a visual "spinner" to the user.
 *
 * @param send_err   The result code from the send function (UDS_OK or error).
 * @param msg        Log message to display alongside the spinner (e.g., "Reading Data").
 * @param timeout_ms Maximum time to wait for a response in milliseconds.
 *
 * @return int 0 on success (Positive Response), -1 on failure (Timeout, Send Error, or NRC).
 */
int uds_wait_transaction_result(UDSErr_t send_err, const char *msg, uint32_t timeout_ms);

/**
 * @brief Standardized UDS Transaction Macro.
 * @details A convenience macro to execute a full UDS request-response cycle safely.
 *          It performs the following steps:
 *          1. `uds_prepare_request()`: Clears state.
 *          2. `uds_wait_transaction_result(...)`: Executes the send call passed as an argument,
 *             then blocks waiting for the result.
 *
 *          @note Uses the C comma operator to enforce sequencing.
 *
 * @param send_call The UDS library function call (e.g., `UDSSendIOControl(...)`).
 * @param msg       String description for the UI wait spinner.
 * @return int 0 on success, -1 on failure.
 */
#define UDS_TRANSACTION(send_call, msg) \
    (uds_prepare_request(), uds_wait_transaction_result((send_call), (msg), 1000))

/**
 * @brief Standardized UDS Transaction Macro with custom timeout.
 * @see UDS_TRANSACTION
 *
 * @param send_call The UDS library function call.
 * @param msg       String description for the UI wait spinner.
 * @param ms        Timeout in milliseconds.
 * @return int 0 on success, -1 on failure.
 */
#define UDS_TRANSACTION_TIMEOUT(send_call, msg, ms) \
    (uds_prepare_request(), uds_wait_transaction_result((send_call), (msg), (ms)))

/* ==========================================================================
 * Public API - Low Level
 * ========================================================================== */

/**
 * @brief Drives the UDS stack state machine.
 * @details Must be called periodically (e.g., inside the shell loop) to process
 *          incoming CAN frames and handle ISO-TP timeouts.
 */
void uds_poll(void);

/**
 * @brief Safely sends a TesterPresent (Heartbeat) message.
 * @details Checks if the client is currently IDLE before sending to avoid
 *          interrupting active transactions (like file transfers).
 *
 * @return int 0 if sent, -1 if skipped (Client busy), -2 on transmission error.
 */
int uds_send_heartbeat_safe(void);

#ifdef __cplusplus
}
#endif

#endif /* UDS_CONTEXT_H */