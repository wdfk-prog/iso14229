/**
 * @file core/client_config.h
 * @brief Runtime configuration definitions for the UDS client.
 * @details This header keeps application-facing configuration independent from
 *          transport implementation details. The transport layer selects the
 *          concrete backend later inside `uds_context.c`.
 */
#ifndef CLIENT_CONFIG_H
#define CLIENT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * Default Configuration (Fallback)
 * ========================================================================== */

#if defined(_WIN32) && defined(UDS_ENABLE_PYCAN_BRIDGE)
#define DEFAULT_TRANSPORT_BACKEND_NAME "pycan_bridge"
#else
#define DEFAULT_TRANSPORT_BACKEND_NAME "socketcan"
#endif

#define DEFAULT_CAN_IF                 "can1"
#define DEFAULT_PYCAN_PYTHON_EXE       "python"
#define DEFAULT_PYCAN_BRIDGE_SCRIPT    "tools/pycan_bridge.py"
#define DEFAULT_PYCAN_INTERFACE        "gs_usb"
#define DEFAULT_PYCAN_CHANNEL          "0"
#define DEFAULT_PYCAN_HOST             "127.0.0.1"
#define DEFAULT_PYCAN_PORT             29536U
#define DEFAULT_PYCAN_BITRATE          500000U
#define DEFAULT_PYCAN_RX_QUEUE_CAP     256U
#define DEFAULT_PYCAN_OPEN_TIMEOUT_MS  4000U
#define DEFAULT_PYCAN_IO_TIMEOUT_MS    250U
#define DEFAULT_TRANSPORT_TIMEOUT_MS   2000U

#define DEFAULT_PHYS_SA                0x7E8U
#define DEFAULT_PHYS_TA                0x7E0U
#define DEFAULT_FUNC_SA                0x7DFU

/* ==========================================================================
 * ISO 14229 Timing Configuration
 * ========================================================================== */

#define CLIENT_DEFAULT_P2_MS           150U
#define CLIENT_DEFAULT_P2_STAR         2000U
#define CLIENT_HEARTBEAT_MS            2000U

/* ==========================================================================
 * Application Limits & Buffer Sizes
 * ========================================================================== */

#define MAX_COMMANDS                   32
#define CMD_MAX_LINE                   4096
#define CMD_MAX_ARGS                   16

#define CLIENT_CFG_STR_SMALL           32U
#define CLIENT_CFG_STR_MEDIUM          64U
#define CLIENT_CFG_STR_LARGE           128U
#define CLIENT_CFG_STR_XL              256U

/* ==========================================================================
 * Runtime Configuration Structure
 * ========================================================================== */

typedef enum {
    CLIENT_BACKEND_SOCKETCAN = 0,
    CLIENT_BACKEND_PYCAN_BRIDGE = 1,
} client_transport_backend_t;

typedef struct {
    char if_name[CLIENT_CFG_STR_SMALL];
} client_socketcan_config_t;

typedef struct {
    char python_exe[CLIENT_CFG_STR_MEDIUM];
    char bridge_script[CLIENT_CFG_STR_XL];
    char interface_name[CLIENT_CFG_STR_SMALL];
    char channel_name[CLIENT_CFG_STR_MEDIUM];
    char host[CLIENT_CFG_STR_SMALL];
    uint16_t port;
    uint32_t bitrate;
    uint32_t rx_queue_capacity;
    uint32_t open_timeout_ms;
    uint32_t io_timeout_ms;
    bool auto_spawn;
    bool use_canfd;
    bool use_brs;
    bool use_extended_ids;
    bool debug_tcp_mode;
} client_pycan_bridge_config_t;

typedef struct {
    client_transport_backend_t backend;
    uint32_t phys_sa;
    uint32_t phys_ta;
    uint32_t func_sa;
    uint32_t timeout_ms;
    client_socketcan_config_t socketcan;
    client_pycan_bridge_config_t pycan_bridge;
} client_runtime_config_t;

extern client_runtime_config_t g_uds_cfg;

const char *client_config_backend_name(client_transport_backend_t backend);
void client_config_parse_args(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* CLIENT_CONFIG_H */
