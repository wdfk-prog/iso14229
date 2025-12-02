/**
 * @file core/response_registry.h
 * @brief Registry for UDS Response Handlers.
 * @details Allows services to subscribe to specific Response SIDs (e.g., 0x71).
 */

#ifndef RESPONSE_REGISTRY_H
#define RESPONSE_REGISTRY_H

#include "../iso14229.h"

/**
 * @brief Response Handler Callback.
 * @param client Pointer to the UDS client (to access recv_buf).
 */
typedef void (*uds_res_handler_t)(UDSClient_t *client);

/**
 * @brief Initialize the registry.
 */
void response_registry_init(void);

/**
 * @brief Register a handler for a specific Response SID.
 * @param sid Response SID (e.g., 0x71 for RoutineControl).
 * @param handler Callback function.
 * @return 0 on success, -1 on failure.
 */
int response_register(uint8_t sid, uds_res_handler_t handler);

/**
 * @brief Dispatch a received response to the registered handler.
 * @param client UDS Client instance.
 */
void response_dispatch(UDSClient_t *client);

#endif /* RESPONSE_REGISTRY_H */