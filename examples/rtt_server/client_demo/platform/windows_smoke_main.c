/**
 * @file windows_smoke_main.c
 * @brief Minimal Windows smoke executable for Task 6B / Task 7 validation.
 * @details Adds optional `client_smoke.conf` loading while keeping the
 *          original environment-variable override behavior intact.
 *
 * Configuration precedence:
 *   1. Built-in defaults
 *   2. Config file (`./client_smoke.conf`, legacy `./tsmaster_smoke.conf`, or `UDS_SMOKE_CONF`)
 *   3. Environment variables (`UDS_SMOKE_*`)
 */

#include "platform.h"
#include "../transport/transport.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMOKE_DEFAULT_PHYS_RX_ID   0x7E8u
#define SMOKE_DEFAULT_PHYS_TX_ID   0x7E0u
#define SMOKE_DEFAULT_FUNC_TX_ID   0x7DFu
#define SMOKE_DEFAULT_APP_NAME     "UDSClient"
#define SMOKE_DEFAULT_HW_NAME      "TSMaster" /* legacy backend default only */
#define SMOKE_DEFAULT_CAN_BAUD     500.0f
#define SMOKE_DEFAULT_CANFD_ARB    500.0f
#define SMOKE_DEFAULT_CANFD_DATA   2000.0f
#define SMOKE_DEFAULT_PYTHON_EXE   "python"
#define SMOKE_DEFAULT_BRIDGE_SCRIPT "client_demo/tools/pycan_bridge.py"
#define SMOKE_DEFAULT_PYCAN_IF     "gs_usb"
#define SMOKE_DEFAULT_PYCAN_CH     "0"
#define SMOKE_DEFAULT_PYCAN_HOST   "127.0.0.1"
#define SMOKE_DEFAULT_PYCAN_PORT   29536u
#define SMOKE_DEFAULT_PYCAN_BITRATE 500000u
#define SMOKE_DEFAULT_PYCAN_RXQ    256u
#define SMOKE_DEFAULT_PYCAN_OPEN_TIMEOUT_MS 4000u
#define SMOKE_DEFAULT_PYCAN_IO_TIMEOUT_MS   250u

#define SMOKE_MAX_NAME_LEN         64u
#define SMOKE_MAX_PATH_LEN         260u
#define SMOKE_MAX_LINE_LEN         512u
#define SMOKE_MAX_CHANNEL_LEN      128u

typedef struct {
    char app_name[SMOKE_MAX_NAME_LEN];
    char hw_device_name[SMOKE_MAX_NAME_LEN];
    char conf_path[SMOKE_MAX_PATH_LEN];
    char pycan_python_exe[SMOKE_MAX_PATH_LEN];
    char pycan_bridge_script[SMOKE_MAX_PATH_LEN];
    char pycan_interface[SMOKE_MAX_NAME_LEN];
    char pycan_channel[SMOKE_MAX_CHANNEL_LEN];
    char pycan_host[SMOKE_MAX_NAME_LEN];
    uint32_t phys_rx_id;
    uint32_t phys_tx_id;
    uint32_t func_tx_id;
    uint8_t app_channel_index;
    int32_t hw_device_type;
    int32_t hw_device_sub_type;
    int32_t hw_index;
    int32_t hw_channel_index;
    float can_baudrate_kbps;
    float canfd_arb_baudrate_kbps;
    float canfd_data_baudrate_kbps;
    uint32_t pycan_port;
    uint32_t pycan_bitrate;
    uint32_t pycan_rx_queue_capacity;
    uint32_t pycan_open_timeout_ms;
    uint32_t pycan_io_timeout_ms;
    int use_canfd;
    int use_brs;
    int install_term_resistor;
    int use_extended_ids;
    int pycan_auto_spawn;
    int pycan_debug_tcp_mode;
    int conf_loaded;
} smoke_settings_t;

static void smoke_copy_string(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0u;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static char *smoke_trim_inplace(char *text)
{
    char *end;

    if (text == NULL) {
        return NULL;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1u;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }

    return text;
}

static int smoke_parse_bool_text(const char *value, int *out_value)
{
    if (value == NULL || out_value == NULL) {
        return 0;
    }

    if ((value[0] == '1' && value[1] == '\0') ||
        strcmp(value, "y") == 0 || strcmp(value, "Y") == 0 ||
        strcmp(value, "yes") == 0 || strcmp(value, "YES") == 0 ||
        strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0 ||
        strcmp(value, "on") == 0 || strcmp(value, "ON") == 0) {
        *out_value = 1;
        return 1;
    }

    if ((value[0] == '0' && value[1] == '\0') ||
        strcmp(value, "n") == 0 || strcmp(value, "N") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "NO") == 0 ||
        strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "OFF") == 0) {
        *out_value = 0;
        return 1;
    }

    return 0;
}

static int smoke_parse_u32_text(const char *value, uint32_t *out_value)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || out_value == NULL) {
        return 0;
    }

    parsed = strtoul(value, &end, 0);
    if (end == value || (end != NULL && *smoke_trim_inplace(end) != '\0')) {
        return 0;
    }

    *out_value = (uint32_t)parsed;
    return 1;
}

static int smoke_parse_i32_text(const char *value, int32_t *out_value)
{
    char *end = NULL;
    long parsed;

    if (value == NULL || out_value == NULL) {
        return 0;
    }

    parsed = strtol(value, &end, 0);
    if (end == value || (end != NULL && *smoke_trim_inplace(end) != '\0')) {
        return 0;
    }

    *out_value = (int32_t)parsed;
    return 1;
}

static int smoke_parse_float_text(const char *value, float *out_value)
{
    char *end = NULL;
    double parsed;

    if (value == NULL || out_value == NULL) {
        return 0;
    }

    parsed = strtod(value, &end);
    if (end == value || (end != NULL && *smoke_trim_inplace(end) != '\0')) {
        return 0;
    }

    *out_value = (float)parsed;
    return 1;
}

static const char *smoke_get_env_nonempty(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return NULL;
    }

    return value;
}

static int smoke_read_bool_env(const char *name, int *out_value)
{
    const char *value = smoke_get_env_nonempty(name);
    int parsed = 0;

    if (value == NULL) {
        return 0;
    }

    if (!smoke_parse_bool_text(value, &parsed)) {
        return 0;
    }

    *out_value = parsed;
    return 1;
}

static int smoke_read_u32_env(const char *name, uint32_t *out_value)
{
    const char *value = smoke_get_env_nonempty(name);

    if (value == NULL) {
        return 0;
    }

    return smoke_parse_u32_text(value, out_value);
}

static int smoke_read_i32_env(const char *name, int32_t *out_value)
{
    const char *value = smoke_get_env_nonempty(name);

    if (value == NULL) {
        return 0;
    }

    return smoke_parse_i32_text(value, out_value);
}

static int smoke_read_float_env(const char *name, float *out_value)
{
    const char *value = smoke_get_env_nonempty(name);

    if (value == NULL) {
        return 0;
    }

    return smoke_parse_float_text(value, out_value);
}

static int smoke_parse_tcp_port_text(const char *value, uint32_t *out_value)
{
    uint32_t parsed;

    if (!smoke_parse_u32_text(value, &parsed)) {
        return 0;
    }
    if (parsed == 0u || parsed > 65535u) {
        return 0;
    }

    *out_value = parsed;
    return 1;
}

static void smoke_settings_init_defaults(smoke_settings_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    smoke_copy_string(cfg->app_name, sizeof(cfg->app_name), SMOKE_DEFAULT_APP_NAME);
    smoke_copy_string(cfg->hw_device_name, sizeof(cfg->hw_device_name), SMOKE_DEFAULT_HW_NAME);
    cfg->phys_rx_id = SMOKE_DEFAULT_PHYS_RX_ID;
    cfg->phys_tx_id = SMOKE_DEFAULT_PHYS_TX_ID;
    cfg->func_tx_id = SMOKE_DEFAULT_FUNC_TX_ID;
    cfg->app_channel_index = 0u;
    cfg->hw_device_type = 0;
    cfg->hw_device_sub_type = 0;
    cfg->hw_index = 0;
    cfg->hw_channel_index = 0;
    cfg->can_baudrate_kbps = SMOKE_DEFAULT_CAN_BAUD;
    cfg->canfd_arb_baudrate_kbps = SMOKE_DEFAULT_CANFD_ARB;
    cfg->canfd_data_baudrate_kbps = SMOKE_DEFAULT_CANFD_DATA;
    smoke_copy_string(cfg->pycan_python_exe, sizeof(cfg->pycan_python_exe), SMOKE_DEFAULT_PYTHON_EXE);
    smoke_copy_string(cfg->pycan_bridge_script, sizeof(cfg->pycan_bridge_script), SMOKE_DEFAULT_BRIDGE_SCRIPT);
    smoke_copy_string(cfg->pycan_interface, sizeof(cfg->pycan_interface), SMOKE_DEFAULT_PYCAN_IF);
    smoke_copy_string(cfg->pycan_channel, sizeof(cfg->pycan_channel), SMOKE_DEFAULT_PYCAN_CH);
    smoke_copy_string(cfg->pycan_host, sizeof(cfg->pycan_host), SMOKE_DEFAULT_PYCAN_HOST);
    cfg->pycan_port = SMOKE_DEFAULT_PYCAN_PORT;
    cfg->pycan_bitrate = SMOKE_DEFAULT_PYCAN_BITRATE;
    cfg->pycan_rx_queue_capacity = SMOKE_DEFAULT_PYCAN_RXQ;
    cfg->pycan_open_timeout_ms = SMOKE_DEFAULT_PYCAN_OPEN_TIMEOUT_MS;
    cfg->pycan_io_timeout_ms = SMOKE_DEFAULT_PYCAN_IO_TIMEOUT_MS;
    cfg->use_canfd = 0;
    cfg->use_brs = 0;
    cfg->install_term_resistor = 0;
    cfg->use_extended_ids = 0;
    cfg->pycan_auto_spawn = 1;
    cfg->pycan_debug_tcp_mode = 0;
    cfg->conf_loaded = 0;
}

static void smoke_settings_apply_env(smoke_settings_t *cfg)
{
    const char *value;
    uint32_t u32_value;
    int32_t i32_value;
    float f32_value;
    int bool_value;

    if (cfg == NULL) {
        return;
    }

    value = smoke_get_env_nonempty("UDS_SMOKE_APP_NAME");
    if (value != NULL) {
        smoke_copy_string(cfg->app_name, sizeof(cfg->app_name), value);
    }

    value = smoke_get_env_nonempty("UDS_SMOKE_HW_NAME");
    if (value != NULL) {
        smoke_copy_string(cfg->hw_device_name, sizeof(cfg->hw_device_name), value);
    }

    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_PYTHON");
    if (value != NULL) {
        smoke_copy_string(cfg->pycan_python_exe, sizeof(cfg->pycan_python_exe), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_BRIDGE_SCRIPT");
    if (value != NULL) {
        smoke_copy_string(cfg->pycan_bridge_script, sizeof(cfg->pycan_bridge_script), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_INTERFACE");
    if (value != NULL) {
        smoke_copy_string(cfg->pycan_interface, sizeof(cfg->pycan_interface), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_CHANNEL");
    if (value != NULL) {
        smoke_copy_string(cfg->pycan_channel, sizeof(cfg->pycan_channel), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_HOST");
    if (value != NULL) {
        smoke_copy_string(cfg->pycan_host, sizeof(cfg->pycan_host), value);
    }

    if (smoke_read_u32_env("UDS_SMOKE_APP_CHANNEL", &u32_value)) {
        cfg->app_channel_index = (uint8_t)u32_value;
    }
    if (smoke_read_i32_env("UDS_SMOKE_HW_DEVICE_TYPE", &i32_value)) {
        cfg->hw_device_type = i32_value;
    }
    if (smoke_read_i32_env("UDS_SMOKE_HW_DEVICE_SUBTYPE", &i32_value)) {
        cfg->hw_device_sub_type = i32_value;
    }
    if (smoke_read_i32_env("UDS_SMOKE_HW_INDEX", &i32_value)) {
        cfg->hw_index = i32_value;
    }
    if (smoke_read_i32_env("UDS_SMOKE_HW_CHANNEL", &i32_value)) {
        cfg->hw_channel_index = i32_value;
    }
    if (smoke_read_float_env("UDS_SMOKE_CAN_BAUD", &f32_value)) {
        cfg->can_baudrate_kbps = f32_value;
    }
    if (smoke_read_float_env("UDS_SMOKE_CANFD_ARB", &f32_value)) {
        cfg->canfd_arb_baudrate_kbps = f32_value;
    }
    if (smoke_read_float_env("UDS_SMOKE_CANFD_DATA", &f32_value)) {
        cfg->canfd_data_baudrate_kbps = f32_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_USE_CANFD", &bool_value)) {
        cfg->use_canfd = bool_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_USE_BRS", &bool_value)) {
        cfg->use_brs = bool_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_TERM", &bool_value)) {
        cfg->install_term_resistor = bool_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_EXT_IDS", &bool_value)) {
        cfg->use_extended_ids = bool_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PHYS_RX_ID", &u32_value)) {
        cfg->phys_rx_id = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PHYS_TX_ID", &u32_value)) {
        cfg->phys_tx_id = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_FUNC_TX_ID", &u32_value)) {
        cfg->func_tx_id = u32_value;
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_PYCAN_PORT");
    if (value != NULL && smoke_parse_tcp_port_text(value, &u32_value)) {
        cfg->pycan_port = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PYCAN_BITRATE", &u32_value)) {
        cfg->pycan_bitrate = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PYCAN_RX_QUEUE", &u32_value)) {
        cfg->pycan_rx_queue_capacity = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PYCAN_OPEN_TIMEOUT_MS", &u32_value)) {
        cfg->pycan_open_timeout_ms = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_PYCAN_IO_TIMEOUT_MS", &u32_value)) {
        cfg->pycan_io_timeout_ms = u32_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_PYCAN_AUTO_SPAWN", &bool_value)) {
        cfg->pycan_auto_spawn = bool_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_PYCAN_DEBUG_TCP", &bool_value)) {
        cfg->pycan_debug_tcp_mode = bool_value;
    }
}

static int smoke_apply_conf_kv(smoke_settings_t *cfg, const char *key, const char *value)
{
    uint32_t u32_value;
    int32_t i32_value;
    float f32_value;
    int bool_value;

    if (cfg == NULL || key == NULL || value == NULL) {
        return 0;
    }

    if (strcmp(key, "app_name") == 0) {
        smoke_copy_string(cfg->app_name, sizeof(cfg->app_name), value);
        return 1;
    }
    if (strcmp(key, "hw_name") == 0 || strcmp(key, "hw_device_name") == 0) {
        smoke_copy_string(cfg->hw_device_name, sizeof(cfg->hw_device_name), value);
        return 1;
    }
    if (strcmp(key, "pycan_python") == 0 || strcmp(key, "pycan_python_exe") == 0) {
        smoke_copy_string(cfg->pycan_python_exe, sizeof(cfg->pycan_python_exe), value);
        return 1;
    }
    if (strcmp(key, "pycan_bridge") == 0 || strcmp(key, "pycan_bridge_script") == 0) {
        smoke_copy_string(cfg->pycan_bridge_script, sizeof(cfg->pycan_bridge_script), value);
        return 1;
    }
    if (strcmp(key, "pycan_interface") == 0) {
        smoke_copy_string(cfg->pycan_interface, sizeof(cfg->pycan_interface), value);
        return 1;
    }
    if (strcmp(key, "pycan_channel") == 0) {
        smoke_copy_string(cfg->pycan_channel, sizeof(cfg->pycan_channel), value);
        return 1;
    }
    if (strcmp(key, "pycan_host") == 0) {
        smoke_copy_string(cfg->pycan_host, sizeof(cfg->pycan_host), value);
        return 1;
    }
    if (strcmp(key, "app_channel") == 0 || strcmp(key, "app_channel_index") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->app_channel_index = (uint8_t)u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "hw_device_type") == 0) {
        if (smoke_parse_i32_text(value, &i32_value)) {
            cfg->hw_device_type = i32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "hw_device_subtype") == 0 || strcmp(key, "hw_device_sub_type") == 0) {
        if (smoke_parse_i32_text(value, &i32_value)) {
            cfg->hw_device_sub_type = i32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "hw_index") == 0) {
        if (smoke_parse_i32_text(value, &i32_value)) {
            cfg->hw_index = i32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "hw_channel") == 0 || strcmp(key, "hw_channel_index") == 0) {
        if (smoke_parse_i32_text(value, &i32_value)) {
            cfg->hw_channel_index = i32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "can_baud") == 0 || strcmp(key, "can_baud_kbps") == 0) {
        if (smoke_parse_float_text(value, &f32_value)) {
            cfg->can_baudrate_kbps = f32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "canfd_arb") == 0 || strcmp(key, "canfd_arb_kbps") == 0) {
        if (smoke_parse_float_text(value, &f32_value)) {
            cfg->canfd_arb_baudrate_kbps = f32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "canfd_data") == 0 || strcmp(key, "canfd_data_kbps") == 0) {
        if (smoke_parse_float_text(value, &f32_value)) {
            cfg->canfd_data_baudrate_kbps = f32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "use_canfd") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->use_canfd = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "use_brs") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->use_brs = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "install_term_resistor") == 0 || strcmp(key, "term") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->install_term_resistor = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "use_extended_ids") == 0 || strcmp(key, "ext_ids") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->use_extended_ids = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "phys_rx_id") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->phys_rx_id = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "phys_tx_id") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->phys_tx_id = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "func_tx_id") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->func_tx_id = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_port") == 0) {
        if (smoke_parse_tcp_port_text(value, &u32_value)) {
            cfg->pycan_port = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_bitrate") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->pycan_bitrate = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_rx_queue") == 0 || strcmp(key, "pycan_rx_queue_capacity") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->pycan_rx_queue_capacity = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_open_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->pycan_open_timeout_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_io_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->pycan_io_timeout_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_auto_spawn") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->pycan_auto_spawn = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "pycan_debug_tcp") == 0 || strcmp(key, "pycan_ipc_tcp") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->pycan_debug_tcp_mode = bool_value;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int smoke_try_load_conf(smoke_settings_t *cfg, const char *path)
{
    FILE *fp;
    char line[SMOKE_MAX_LINE_LEN];
    unsigned long line_no = 0u;

    if (cfg == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *work;
        char *sep;
        char *key;
        char *value;

        ++line_no;
        work = smoke_trim_inplace(line);
        if (work[0] == '\0' || work[0] == '#' || work[0] == ';') {
            continue;
        }

        sep = strchr(work, '=');
        if (sep == NULL) {
            fprintf(stderr, "[client_smoke] ignore malformed conf line %lu: %s\n", line_no, work);
            continue;
        }

        *sep = '\0';
        key = smoke_trim_inplace(work);
        value = smoke_trim_inplace(sep + 1);

        if (value[0] == '"') {
            size_t len = strlen(value);
            if (len >= 2u && value[len - 1u] == '"') {
                value[len - 1u] = '\0';
                ++value;
            }
        }

        if (!smoke_apply_conf_kv(cfg, key, value)) {
            fprintf(stderr, "[client_smoke] ignore invalid conf entry line %lu: %s=%s\n",
                    line_no,
                    key,
                    value);
        }
    }

    fclose(fp);
    cfg->conf_loaded = 1;
    smoke_copy_string(cfg->conf_path, sizeof(cfg->conf_path), path);
    return 1;
}

static void smoke_try_load_default_conf(smoke_settings_t *cfg)
{
    const char *env_path;

    if (cfg == NULL) {
        return;
    }

    env_path = smoke_get_env_nonempty("UDS_SMOKE_CONF");
    if (env_path != NULL) {
        if (!smoke_try_load_conf(cfg, env_path)) {
            fprintf(stderr, "[client_smoke] warning: failed to load UDS_SMOKE_CONF=%s\n", env_path);
        }
        return;
    }

    if (!smoke_try_load_conf(cfg, "./client_smoke.conf")) {
        (void)smoke_try_load_conf(cfg, "./tsmaster_smoke.conf");
    }
}

int main(void)
{
    uds_transport_t tp;
    unsigned char storage[UDS_TRANSPORT_STORAGE_CAPACITY];
    uds_transport_open_cfg_t open_cfg;
    smoke_settings_t smoke_cfg;
    int rc;

    smoke_settings_init_defaults(&smoke_cfg);
    smoke_try_load_default_conf(&smoke_cfg);
    smoke_settings_apply_env(&smoke_cfg);

    memset(&open_cfg, 0, sizeof(open_cfg));
    open_cfg.phys_sa = smoke_cfg.phys_rx_id;
    open_cfg.phys_ta = smoke_cfg.phys_tx_id;
    open_cfg.func_sa = smoke_cfg.func_tx_id;

#if defined(UDS_TRANSPORT_ENABLE_PYCAN_BRIDGE)
    {
        uds_transport_pycan_bridge_cfg_t backend_cfg;
        memset(&backend_cfg, 0, sizeof(backend_cfg));
        backend_cfg.python_exe = smoke_cfg.pycan_python_exe;
        backend_cfg.bridge_script = smoke_cfg.pycan_bridge_script;
        backend_cfg.interface_name = smoke_cfg.pycan_interface;
        backend_cfg.channel_name = smoke_cfg.pycan_channel;
        backend_cfg.host = smoke_cfg.pycan_host;
        backend_cfg.port = (uint16_t)smoke_cfg.pycan_port;
        backend_cfg.bitrate = smoke_cfg.pycan_bitrate;
        backend_cfg.rx_queue_capacity = smoke_cfg.pycan_rx_queue_capacity;
        backend_cfg.open_timeout_ms = smoke_cfg.pycan_open_timeout_ms;
        backend_cfg.io_timeout_ms = smoke_cfg.pycan_io_timeout_ms;
        backend_cfg.auto_spawn = smoke_cfg.pycan_auto_spawn != 0;
        backend_cfg.use_canfd = smoke_cfg.use_canfd != 0;
        backend_cfg.use_brs = smoke_cfg.use_brs != 0;
        backend_cfg.use_extended_ids = smoke_cfg.use_extended_ids != 0;
        backend_cfg.ipc_mode = smoke_cfg.pycan_debug_tcp_mode != 0
                                   ? UDS_PYCAN_BRIDGE_IPC_TCP_JSONL
                                   : UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL;

        open_cfg.backend = UDS_TRANSPORT_BACKEND_PYCAN_BRIDGE;
        open_cfg.backend_cfg = &backend_cfg;

        uds_transport_init(&tp);
        rc = uds_transport_bind_storage(&tp, storage, sizeof(storage));
        if (rc != 0) {
            fprintf(stderr, "[client_smoke] bind_storage failed rc=%d\n", rc);
            return 1;
        }

        printf("[client_smoke] Windows smoke starting\n");
        printf("[client_smoke] backend=PYCAN_BRIDGE arch=x64 tick_ms=%u\n", platform_tick_ms());
        printf("[client_smoke] pycan if=%s channel=%s bitrate=%u auto_spawn=%d ipc=%s ext=%d canfd=%d brs=%d\n",
               backend_cfg.interface_name,
               backend_cfg.channel_name,
               (unsigned)backend_cfg.bitrate,
               backend_cfg.auto_spawn ? 1 : 0,
               backend_cfg.ipc_mode == UDS_PYCAN_BRIDGE_IPC_TCP_JSONL ? "tcp" : "stdio",
               backend_cfg.use_extended_ids ? 1 : 0,
               backend_cfg.use_canfd ? 1 : 0,
               backend_cfg.use_brs ? 1 : 0);

        rc = uds_transport_open(&tp, &open_cfg);
        if (rc != 0) {
            fprintf(stderr, "[client_smoke] transport open failed rc=%d last_error=%d\n",
                    rc,
                    uds_transport_get_last_error(&tp));
            fprintf(stderr, "[client_smoke] tip: ensure python and client_demo/tools/pycan_bridge.py (or tools/pycan_bridge.py) are reachable, and install python-can dependencies\n");
            return 2;
        }
    }
#else
    {
        uds_transport_tsmaster_cfg_t backend_cfg;
        backend_cfg.app_name = smoke_cfg.app_name;
        backend_cfg.hw_device_name = smoke_cfg.hw_device_name;
        backend_cfg.app_channel_index = smoke_cfg.app_channel_index;
        backend_cfg.hw_device_type = smoke_cfg.hw_device_type;
        backend_cfg.hw_device_sub_type = smoke_cfg.hw_device_sub_type;
        backend_cfg.hw_index = smoke_cfg.hw_index;
        backend_cfg.hw_channel_index = smoke_cfg.hw_channel_index;
        backend_cfg.can_baudrate_kbps = smoke_cfg.can_baudrate_kbps;
        backend_cfg.canfd_arb_baudrate_kbps = smoke_cfg.canfd_arb_baudrate_kbps;
        backend_cfg.canfd_data_baudrate_kbps = smoke_cfg.canfd_data_baudrate_kbps;
        backend_cfg.use_canfd = smoke_cfg.use_canfd != 0;
        backend_cfg.use_brs = smoke_cfg.use_brs != 0;
        backend_cfg.install_term_resistor = smoke_cfg.install_term_resistor != 0;
        backend_cfg.use_extended_ids = smoke_cfg.use_extended_ids != 0;

        open_cfg.backend = UDS_TRANSPORT_BACKEND_TSMASTER;
        open_cfg.backend_cfg = &backend_cfg;

        uds_transport_init(&tp);
        rc = uds_transport_bind_storage(&tp, storage, sizeof(storage));
        if (rc != 0) {
            fprintf(stderr, "[client_smoke] bind_storage failed rc=%d\n", rc);
            return 1;
        }

        printf("[client_smoke] Windows smoke starting\n");
        printf("[client_smoke] backend=TSMASTER_API arch=x64 tick_ms=%u\n", platform_tick_ms());
        if (smoke_cfg.conf_loaded) {
            printf("[client_smoke] conf=%s\n", smoke_cfg.conf_path);
        } else {
            printf("[client_smoke] conf=(not loaded, using defaults/env)\n");
        }
        printf("[client_smoke] app=%s hw=%s app_ch=%u hw_type=%d hw_sub=%d hw_idx=%d hw_ch=%d\n",
               backend_cfg.app_name,
               backend_cfg.hw_device_name,
               (unsigned)backend_cfg.app_channel_index,
               (int)backend_cfg.hw_device_type,
               (int)backend_cfg.hw_device_sub_type,
               (int)backend_cfg.hw_index,
               (int)backend_cfg.hw_channel_index);
        printf("[client_smoke] ids phys_rx=0x%X phys_tx=0x%X func_tx=0x%X canfd=%d brs=%d ext=%d term=%d baud=%.1f\n",
               (unsigned)open_cfg.phys_sa,
               (unsigned)open_cfg.phys_ta,
               (unsigned)open_cfg.func_sa,
               backend_cfg.use_canfd ? 1 : 0,
               backend_cfg.use_brs ? 1 : 0,
               backend_cfg.use_extended_ids ? 1 : 0,
               backend_cfg.install_term_resistor ? 1 : 0,
               backend_cfg.can_baudrate_kbps);

        rc = uds_transport_open(&tp, &open_cfg);
        if (rc != 0) {
            fprintf(stderr, "[client_smoke] transport open failed rc=%d last_error=%d\n",
                    rc,
                    uds_transport_get_last_error(&tp));
            fprintf(stderr, "[client_smoke] tip: set UDS_TSMASTER_DLL_PATH or check mapping/hardware state in TSMaster\n");
            return 2;
        }
    }
#endif

    printf("[client_smoke] transport open succeeded\n");
    rc = uds_transport_poll(&tp);
    printf("[client_smoke] first poll rc=%d last_error=%d\n", rc, uds_transport_get_last_error(&tp));

    uds_transport_close(&tp);
    printf("[client_smoke] transport closed cleanly\n");
    return 0;
}

