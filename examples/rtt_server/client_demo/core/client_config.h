/**
 * @file core/client_config.h
 * @brief Runtime Configuration Definitions for the UDS Client.
 * @details This header defines the default connection parameters, system limits,
 *          and the runtime configuration structure used to maintain the
 *          state of the UDS connection.
 * @author wdfk-prog
 * @version 1.0
 * @date 2025-12-02
 *
 * @copyright Copyright (c) 2025
 */

#ifndef CLIENT_CONFIG_H
#define CLIENT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ==========================================================================
 * Default Configuration (Fallback)
 * ========================================================================== */

/** @brief Default SocketCAN interface name if not provided via CLI. */
#define DEFAULT_CAN_IF      "can1"

/** @brief Default Client Physical Source Address (Tester ID). */
#define DEFAULT_PHYS_SA     0x7E8

/** @brief Default Server Physical Target Address (ECU Rx ID). */
#define DEFAULT_PHYS_TA     0x7E0

/** @brief Default Client Functional Source Address (Broadcast ID). */
#define DEFAULT_FUNC_SA     0x7DF

/* ==========================================================================
 * ISO 14229 Timing Configuration
 * ========================================================================== */

/**
 * @brief Default P2_Client_Max timeout in milliseconds.
 * @details Time the client waits for an initial response from the server.
 *          ISO 14229-2 standard default is often 150ms or 50ms depending on bus.
 */
#define CLIENT_DEFAULT_P2_MS    150

/**
 * @brief Default P2*_Client_Max timeout in milliseconds.
 * @details Time the client waits after receiving an NRC 0x78 (Response Pending).
 *          ISO 14229-2 standard default is typically 5000ms (modified here to 2000ms).
 */
#define CLIENT_DEFAULT_P2_STAR  2000

/**
 * @brief TesterPresent (0x3E) Heartbeat interval in milliseconds.
 * @details Must be sent periodically to keep non-default sessions active.
 *          Typically set to ~2000ms (S3_Client time).
 */
#define CLIENT_HEARTBEAT_MS     2000

/* ==========================================================================
 * Application Limits & Buffer Sizes
 * ========================================================================== */

/** @brief Maximum number of registered local shell commands. */
#define MAX_COMMANDS            32

/** @brief Maximum length of a single command line input string. */
#define CMD_MAX_LINE            4096

/** @brief Maximum number of arguments parsed in a single command. */
#define CMD_MAX_ARGS            16

/* ==========================================================================
 * Runtime Configuration Structure
 * ========================================================================== */

/**
 * @brief Runtime configuration container.
 * @details Stores the actual values used for the connection, which may differ
 *          from the DEFAULT_ macros if overridden by command-line arguments.
 */
typedef struct {
    char if_name[32];   /**< SocketCAN Interface Name (e.g., "can0") */
    uint32_t phys_sa;   /**< Client Physical Source Address (Tester) */
    uint32_t phys_ta;   /**< Server Physical Target Address (ECU) */
    uint32_t func_sa;   /**< Functional/Broadcast Source Address */
} client_runtime_config_t;

/* ==========================================================================
 * Global Variables & API
 * ========================================================================== */

/**
 * @brief Global Configuration Instance.
 * @details This variable is the "Single Source of Truth" for the application's
 *          network configuration. It is populated in main/config parser and
 *          read by the UDS context.
 */
extern client_runtime_config_t g_uds_cfg;

/**
 * @brief Parses command line arguments to override default configurations.
 * @details Supports standard getopt flags (e.g., -i, -s, -t) to modify
 *          interface and address settings at runtime.
 *
 * @param argc Argument count from main().
 * @param argv Argument vector from main().
 */
void client_config_parse_args(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* CLIENT_CONFIG_H */