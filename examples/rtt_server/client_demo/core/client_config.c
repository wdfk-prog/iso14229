/**
 * @file core/client_config.c
 * @brief Implementation of runtime configuration loading and parsing.
 */

#include "client_config.h"
#include "../utils/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

client_runtime_config_t g_uds_cfg = {
    .backend =
#if defined(_WIN32) && defined(UDS_ENABLE_PYCAN_BRIDGE)
        CLIENT_BACKEND_PYCAN_BRIDGE,
#else
        CLIENT_BACKEND_SOCKETCAN,
#endif
    .phys_sa = DEFAULT_PHYS_SA,
    .phys_ta = DEFAULT_PHYS_TA,
    .func_sa = DEFAULT_FUNC_SA,
    .timeout_ms = DEFAULT_TRANSPORT_TIMEOUT_MS,
    .socketcan = {
        .if_name = DEFAULT_CAN_IF,
    },
    .pycan_bridge = {
        .python_exe = DEFAULT_PYCAN_PYTHON_EXE,
        .bridge_script = DEFAULT_PYCAN_BRIDGE_SCRIPT,
        .interface_name = DEFAULT_PYCAN_INTERFACE,
        .channel_name = DEFAULT_PYCAN_CHANNEL,
        .host = DEFAULT_PYCAN_HOST,
        .port = DEFAULT_PYCAN_PORT,
        .bitrate = DEFAULT_PYCAN_BITRATE,
        .rx_queue_capacity = DEFAULT_PYCAN_RX_QUEUE_CAP,
        .open_timeout_ms = DEFAULT_PYCAN_OPEN_TIMEOUT_MS,
        .io_timeout_ms = DEFAULT_PYCAN_IO_TIMEOUT_MS,
        .auto_spawn = true,
        .use_canfd = false,
        .use_brs = false,
        .use_extended_ids = false,
        .debug_tcp_mode = false,
    },
};

static int str_ieq(const char *lhs, const char *rhs)
{
    unsigned char a;
    unsigned char b;

    if (lhs == NULL || rhs == NULL) {
        return 0;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        a = (unsigned char)*lhs++;
        b = (unsigned char)*rhs++;
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }
    return (*lhs == '\0' && *rhs == '\0') ? 1 : 0;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *endptr = NULL;
    unsigned long value;

    if (text == NULL || out == NULL || *text == '\0') {
        return -1;
    }
    value = strtoul(text, &endptr, 0);
    if (endptr == NULL || *endptr != '\0') {
        return -1;
    }
    if (value > 0xFFFFFFFFUL) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int parse_u16(const char *text, uint16_t *out)
{
    uint32_t tmp;

    if (parse_u32(text, &tmp) != 0 || tmp > 0xFFFFU) {
        return -1;
    }
    *out = (uint16_t)tmp;
    return 0;
}

static int parse_hex_id(const char *text, uint32_t *out)
{
    char *endptr = NULL;
    unsigned long value;

    if (text == NULL || out == NULL || *text == '\0') {
        return -1;
    }
    value = strtoul(text, &endptr, 16);
    if (endptr == NULL || *endptr != '\0') {
        return -1;
    }
    if (value > 0x1FFFFFFFUL) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int parse_backend_name(const char *text, client_transport_backend_t *out)
{
    if (str_ieq(text, "socketcan")) {
        *out = CLIENT_BACKEND_SOCKETCAN;
        return 0;
    }
    if (str_ieq(text, "pycan_bridge") || str_ieq(text, "pycan") || str_ieq(text, "python-can")) {
        *out = CLIENT_BACKEND_PYCAN_BRIDGE;
        return 0;
    }
    return -1;
}

static int expect_value(const char *opt, int argc, char **argv, int *idx, const char **value_out)
{
    if ((*idx + 1) >= argc) {
        fprintf(stderr, "[Config] Missing value for %s\n", opt);
        return -1;
    }
    *idx += 1;
    *value_out = argv[*idx];
    return 0;
}

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n", prog_name);
    printf("\nCore options:\n");
    printf("  -h, --help                 Show this help\n");
    printf("  -b, --backend <name>       socketcan | pycan_bridge (default: %s)\n", DEFAULT_TRANSPORT_BACKEND_NAME);
    printf("  -s, --phys-sa <hex_id>     Client physical source ID (default: %03X)\n", DEFAULT_PHYS_SA);
    printf("  -t, --phys-ta <hex_id>     Server physical target ID (default: %03X)\n", DEFAULT_PHYS_TA);
    printf("  -f, --func-sa <hex_id>     Functional target ID (default: %03X)\n", DEFAULT_FUNC_SA);
    printf("      --timeout-ms <ms>      Transport-level timeout (default: %u)\n", DEFAULT_TRANSPORT_TIMEOUT_MS);
    printf("\nSocketCAN options:\n");
    printf("  -i, --if-name <name>       SocketCAN interface (default: %s)\n", DEFAULT_CAN_IF);
    printf("\npycan_bridge options:\n");
    printf("      --python <exe>         Python executable (default: %s)\n", DEFAULT_PYCAN_PYTHON_EXE);
    printf("      --bridge-script <path> Bridge script path (default: %s)\n", DEFAULT_PYCAN_BRIDGE_SCRIPT);
    printf("      --py-if <name>         python-can interface: gs_usb | slcan (default: %s)\n", DEFAULT_PYCAN_INTERFACE);
    printf("      --py-channel <name>    Channel string. gs_usb: 0; slcan: COM4@9600\n");
    printf("      --bitrate <bps>        Arbitration bitrate (default: %u)\n", DEFAULT_PYCAN_BITRATE);
    printf("      --rx-queue <count>     Host-side RX queue depth (default: %u)\n", DEFAULT_PYCAN_RX_QUEUE_CAP);
    printf("      --open-timeout-ms <ms> Sidecar open timeout (default: %u)\n", DEFAULT_PYCAN_OPEN_TIMEOUT_MS);
    printf("      --io-timeout-ms <ms>   Bridge command timeout (default: %u)\n", DEFAULT_PYCAN_IO_TIMEOUT_MS);
    printf("      --canfd                Enable CAN FD\n");
    printf("      --brs                  Enable bit-rate switching (requires --canfd)\n");
    printf("      --extid                Use extended CAN identifiers\n");
    printf("      --tcp-host <addr>      Debug TCP mode host (default: %s)\n", DEFAULT_PYCAN_HOST);
    printf("      --tcp-port <port>      Debug TCP mode port (default: %u)\n", DEFAULT_PYCAN_PORT);
    printf("      --ipc-tcp              Use debug TCP transport instead of stdio JSONL\n");
    printf("      --no-auto-spawn        Do not spawn the Python sidecar from the C client\n");
    printf("\nExamples:\n");
    printf("  %s -b socketcan -i vcan0 -s 7E8 -t 7E0\n", prog_name);
    printf("  %s -b pycan_bridge --py-if gs_usb --py-channel 0 --bitrate 1000000\n", prog_name);
    printf("  %s -b pycan_bridge --py-if slcan --py-channel COM4@9600 --bitrate 1000000\n", prog_name);
}

static int validate_config(const client_runtime_config_t *cfg)
{
    if (cfg->timeout_ms == 0U) {
        fprintf(stderr, "[Config] timeout_ms must be > 0\n");
        return -1;
    }

    if (cfg->backend == CLIENT_BACKEND_SOCKETCAN) {
        if (cfg->socketcan.if_name[0] == '\0') {
            fprintf(stderr, "[Config] SocketCAN backend requires --if-name\n");
            return -1;
        }
        return 0;
    }

    if (cfg->backend == CLIENT_BACKEND_PYCAN_BRIDGE) {
        const client_pycan_bridge_config_t *py = &cfg->pycan_bridge;

        if (py->interface_name[0] == '\0' || py->channel_name[0] == '\0') {
            fprintf(stderr, "[Config] pycan_bridge requires --py-if and --py-channel\n");
            return -1;
        }
        if (py->auto_spawn && (py->python_exe[0] == '\0' || py->bridge_script[0] == '\0')) {
            fprintf(stderr, "[Config] auto-spawn mode requires non-empty --python and --bridge-script\n");
            return -1;
        }
        if (!str_ieq(py->interface_name, "gs_usb") && !str_ieq(py->interface_name, "slcan")) {
            fprintf(stderr, "[Config] --py-if must be gs_usb or slcan\n");
            return -1;
        }
        if (py->bitrate == 0U) {
            fprintf(stderr, "[Config] --bitrate must be > 0\n");
            return -1;
        }
        if (py->open_timeout_ms == 0U || py->io_timeout_ms == 0U) {
            fprintf(stderr, "[Config] open/io timeout must be > 0\n");
            return -1;
        }
        if (!py->use_canfd && py->use_brs) {
            fprintf(stderr, "[Config] --brs requires --canfd\n");
            return -1;
        }
        if (!py->auto_spawn && !py->debug_tcp_mode) {
            fprintf(stderr, "[Config] --no-auto-spawn requires --ipc-tcp so the bridge is reachable externally\n");
            return -1;
        }
        if (py->debug_tcp_mode && (py->host[0] == '\0' || py->port == 0U)) {
            fprintf(stderr, "[Config] debug TCP mode requires valid --tcp-host and --tcp-port\n");
            return -1;
        }
        return 0;
    }

    fprintf(stderr, "[Config] Unsupported backend value\n");
    return -1;
}

const char *client_config_backend_name(client_transport_backend_t backend)
{
    switch (backend) {
    case CLIENT_BACKEND_SOCKETCAN:
        return "socketcan";
    case CLIENT_BACKEND_PYCAN_BRIDGE:
        return "pycan_bridge";
    default:
        return "unknown";
    }
}

void client_config_parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;
        uint32_t u32 = 0U;
        uint16_t u16 = 0U;
        client_transport_backend_t backend = CLIENT_BACKEND_SOCKETCAN;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--if-name") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.socketcan.if_name, sizeof(g_uds_cfg.socketcan.if_name), value);
        } else if (strcmp(arg, "-b") == 0 || strcmp(arg, "--backend") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            if (parse_backend_name(value, &backend) != 0) {
                fprintf(stderr, "[Config] Unsupported backend: %s\n", value);
                goto fail;
            }
            g_uds_cfg.backend = backend;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--phys-sa") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_hex_id(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid phys_sa: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.phys_sa = u32;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--phys-ta") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_hex_id(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid phys_ta: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.phys_ta = u32;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--func-sa") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_hex_id(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid func_sa: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.func_sa = u32;
        } else if (strcmp(arg, "--timeout-ms") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u32(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid timeout_ms: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.timeout_ms = u32;
        } else if (strcmp(arg, "--python") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.pycan_bridge.python_exe, sizeof(g_uds_cfg.pycan_bridge.python_exe), value);
        } else if (strcmp(arg, "--bridge-script") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.pycan_bridge.bridge_script, sizeof(g_uds_cfg.pycan_bridge.bridge_script), value);
        } else if (strcmp(arg, "--py-if") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.pycan_bridge.interface_name, sizeof(g_uds_cfg.pycan_bridge.interface_name), value);
        } else if (strcmp(arg, "--py-channel") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.pycan_bridge.channel_name, sizeof(g_uds_cfg.pycan_bridge.channel_name), value);
        } else if (strcmp(arg, "--bitrate") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u32(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid bitrate: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.pycan_bridge.bitrate = u32;
        } else if (strcmp(arg, "--rx-queue") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u32(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid rx_queue: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.pycan_bridge.rx_queue_capacity = u32;
        } else if (strcmp(arg, "--open-timeout-ms") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u32(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid open_timeout_ms: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.pycan_bridge.open_timeout_ms = u32;
        } else if (strcmp(arg, "--io-timeout-ms") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u32(value, &u32) != 0) {
                fprintf(stderr, "[Config] Invalid io_timeout_ms: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.pycan_bridge.io_timeout_ms = u32;
        } else if (strcmp(arg, "--tcp-host") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0) {
                goto fail;
            }
            copy_string(g_uds_cfg.pycan_bridge.host, sizeof(g_uds_cfg.pycan_bridge.host), value);
        } else if (strcmp(arg, "--tcp-port") == 0) {
            if (expect_value(arg, argc, argv, &i, &value) != 0 || parse_u16(value, &u16) != 0) {
                fprintf(stderr, "[Config] Invalid tcp port: %s\n", value != NULL ? value : "<missing>");
                goto fail;
            }
            g_uds_cfg.pycan_bridge.port = u16;
        } else if (strcmp(arg, "--canfd") == 0) {
            g_uds_cfg.pycan_bridge.use_canfd = true;
        } else if (strcmp(arg, "--brs") == 0) {
            g_uds_cfg.pycan_bridge.use_brs = true;
        } else if (strcmp(arg, "--extid") == 0) {
            g_uds_cfg.pycan_bridge.use_extended_ids = true;
        } else if (strcmp(arg, "--ipc-tcp") == 0) {
            g_uds_cfg.pycan_bridge.debug_tcp_mode = true;
        } else if (strcmp(arg, "--no-auto-spawn") == 0) {
            g_uds_cfg.pycan_bridge.auto_spawn = false;
        } else {
            fprintf(stderr, "[Config] Unknown option: %s\n", arg);
            goto fail;
        }
    }

    if (validate_config(&g_uds_cfg) != 0) {
        goto fail;
    }

    printf("[Config] backend=%s sa=0x%X ta=0x%X func=0x%X timeout_ms=%u\n",
           client_config_backend_name(g_uds_cfg.backend),
           g_uds_cfg.phys_sa,
           g_uds_cfg.phys_ta,
           g_uds_cfg.func_sa,
           g_uds_cfg.timeout_ms);

    if (g_uds_cfg.backend == CLIENT_BACKEND_SOCKETCAN) {
        printf("[Config] socketcan.if_name=%s\n", g_uds_cfg.socketcan.if_name);
    } else if (g_uds_cfg.backend == CLIENT_BACKEND_PYCAN_BRIDGE) {
        printf("[Config] pycan.if=%s channel=%s bitrate=%u canfd=%d brs=%d extid=%d auto_spawn=%d tcp_debug=%d\n",
               g_uds_cfg.pycan_bridge.interface_name,
               g_uds_cfg.pycan_bridge.channel_name,
               g_uds_cfg.pycan_bridge.bitrate,
               g_uds_cfg.pycan_bridge.use_canfd ? 1 : 0,
               g_uds_cfg.pycan_bridge.use_brs ? 1 : 0,
               g_uds_cfg.pycan_bridge.use_extended_ids ? 1 : 0,
               g_uds_cfg.pycan_bridge.auto_spawn ? 1 : 0,
               g_uds_cfg.pycan_bridge.debug_tcp_mode ? 1 : 0);
    }
    return;

fail:
    print_usage(argv[0]);
    exit(EXIT_FAILURE);
}
