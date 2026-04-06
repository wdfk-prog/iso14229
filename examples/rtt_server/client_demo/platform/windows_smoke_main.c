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
#include "../utils/utils.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
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
#define SMOKE_DEFAULT_PYCAN_BITRATE 1000000u
#define SMOKE_DEFAULT_PYCAN_RXQ    256u
#define SMOKE_DEFAULT_PYCAN_OPEN_TIMEOUT_MS 4000u
#define SMOKE_DEFAULT_PYCAN_IO_TIMEOUT_MS   250u
#define SMOKE_DEFAULT_SESSION_ID            0x03u
#define SMOKE_DEFAULT_SESSION_REPEAT        3u
#define SMOKE_DEFAULT_SESSION_TIMEOUT_MS    2000u
#define SMOKE_DEFAULT_SESSION_GAP_MS        200u
#define SMOKE_DEFAULT_SECURITY_LEVEL        0x01u
#define SMOKE_DEFAULT_SECURITY_REPEAT       1u
#define SMOKE_DEFAULT_SECURITY_TIMEOUT_MS   2000u
#define SMOKE_DEFAULT_SECURITY_GAP_MS       200u
#define SMOKE_DEFAULT_REXEC_REPEAT          1u
#define SMOKE_DEFAULT_REXEC_TIMEOUT_MS      4000u
#define SMOKE_DEFAULT_REXEC_CMD             "ps"
#define SMOKE_DEFAULT_REXEC_EXPECT          ""
#define SMOKE_DEFAULT_FILE_TIMEOUT_MS       5000u
#define SMOKE_DEFAULT_REMOTE_CONSOLE_RID    0xF000u
#define SMOKE_DEFAULT_SECRET_KEY_MASK       0xA5A5A5A5u
#define SMOKE_DEFAULT_MOOP_ADD_FILE         0x01u
#define SMOKE_DEFAULT_MOOP_READ_FILE        0x04u

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
    int run_session_smoke;
    int run_security_smoke;
    int run_rexec_smoke;
    int run_file_smoke;
    uint32_t session_id;
    uint32_t session_repeat;
    uint32_t session_timeout_ms;
    uint32_t session_gap_ms;
    uint32_t security_level;
    uint32_t security_repeat;
    uint32_t security_timeout_ms;
    uint32_t security_gap_ms;
    uint32_t rexec_repeat;
    uint32_t rexec_timeout_ms;
    uint32_t file_timeout_ms;
    char rexec_cmd[SMOKE_MAX_LINE_LEN];
    char rexec_expect[SMOKE_MAX_LINE_LEN];
    char file_local_upload[SMOKE_MAX_PATH_LEN];
    char file_remote_name[SMOKE_MAX_PATH_LEN];
    char file_local_download[SMOKE_MAX_PATH_LEN];
    int conf_loaded;
} smoke_settings_t;

typedef struct {
    volatile int response_done;
    uint8_t response_sid;
    uint8_t response_subfn;
    uint16_t response_rid;
    uint8_t last_nrc;
    UDSErr_t last_err;
    uint16_t recv_size;
    uint8_t recv_buf[UDS_CLIENT_RECV_BUF_SIZE];
} smoke_request_state_t;

typedef struct {
    volatile int response_done;
    uint8_t expected_session;
    uint8_t response_sid;
    uint8_t response_subfn;
    uint8_t last_nrc;
    UDSErr_t last_err;
} smoke_session_state_t;

typedef struct {
    int valid;
    uint32_t p2_ms;
    uint32_t p2_star_ms;
} smoke_client_timing_t;

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

static int smoke_parse_hex_byte_text(const char *value, uint32_t *out_value)
{
    uint32_t parsed;

    if (!smoke_parse_u32_text(value, &parsed)) {
        return 0;
    }
    if (parsed == 0u || parsed > 0x7Fu) {
        return 0;
    }

    *out_value = parsed;
    return 1;
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
    cfg->run_session_smoke = 0;
    cfg->run_security_smoke = 0;
    cfg->run_rexec_smoke = 0;
    cfg->run_file_smoke = 0;
    cfg->session_id = SMOKE_DEFAULT_SESSION_ID;
    cfg->session_repeat = SMOKE_DEFAULT_SESSION_REPEAT;
    cfg->session_timeout_ms = SMOKE_DEFAULT_SESSION_TIMEOUT_MS;
    cfg->session_gap_ms = SMOKE_DEFAULT_SESSION_GAP_MS;
    cfg->security_level = SMOKE_DEFAULT_SECURITY_LEVEL;
    cfg->security_repeat = SMOKE_DEFAULT_SECURITY_REPEAT;
    cfg->security_timeout_ms = SMOKE_DEFAULT_SECURITY_TIMEOUT_MS;
    cfg->security_gap_ms = SMOKE_DEFAULT_SECURITY_GAP_MS;
    cfg->rexec_repeat = SMOKE_DEFAULT_REXEC_REPEAT;
    cfg->rexec_timeout_ms = SMOKE_DEFAULT_REXEC_TIMEOUT_MS;
    cfg->file_timeout_ms = SMOKE_DEFAULT_FILE_TIMEOUT_MS;
    smoke_copy_string(cfg->rexec_cmd, sizeof(cfg->rexec_cmd), SMOKE_DEFAULT_REXEC_CMD);
    smoke_copy_string(cfg->rexec_expect, sizeof(cfg->rexec_expect), SMOKE_DEFAULT_REXEC_EXPECT);
    cfg->file_local_upload[0] = '\0';
    cfg->file_remote_name[0] = '\0';
    cfg->file_local_download[0] = '\0';
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
    if (smoke_read_bool_env("UDS_SMOKE_RUN_SESSION", &bool_value)) {
        cfg->run_session_smoke = bool_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SESSION_REPEAT", &u32_value) && u32_value > 0u) {
        cfg->session_repeat = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SESSION_TIMEOUT_MS", &u32_value) && u32_value > 0u) {
        cfg->session_timeout_ms = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SESSION_GAP_MS", &u32_value)) {
        cfg->session_gap_ms = u32_value;
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_SESSION_ID");
    if (value != NULL && smoke_parse_hex_byte_text(value, &u32_value)) {
        cfg->session_id = u32_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_RUN_SECURITY", &bool_value)) {
        cfg->run_security_smoke = bool_value;
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_SECURITY_LEVEL");
    if (value != NULL && smoke_parse_hex_byte_text(value, &u32_value)) {
        cfg->security_level = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SECURITY_REPEAT", &u32_value) && u32_value > 0u) {
        cfg->security_repeat = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SECURITY_TIMEOUT_MS", &u32_value) && u32_value > 0u) {
        cfg->security_timeout_ms = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_SECURITY_GAP_MS", &u32_value)) {
        cfg->security_gap_ms = u32_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_RUN_REXEC", &bool_value)) {
        cfg->run_rexec_smoke = bool_value;
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_REXEC_CMD");
    if (value != NULL) {
        smoke_copy_string(cfg->rexec_cmd, sizeof(cfg->rexec_cmd), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_REXEC_EXPECT");
    if (value != NULL) {
        smoke_copy_string(cfg->rexec_expect, sizeof(cfg->rexec_expect), value);
    }
    if (smoke_read_u32_env("UDS_SMOKE_REXEC_REPEAT", &u32_value) && u32_value > 0u) {
        cfg->rexec_repeat = u32_value;
    }
    if (smoke_read_u32_env("UDS_SMOKE_REXEC_TIMEOUT_MS", &u32_value) && u32_value > 0u) {
        cfg->rexec_timeout_ms = u32_value;
    }
    if (smoke_read_bool_env("UDS_SMOKE_RUN_FILE", &bool_value)) {
        cfg->run_file_smoke = bool_value;
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_FILE_LOCAL_UPLOAD");
    if (value != NULL) {
        smoke_copy_string(cfg->file_local_upload, sizeof(cfg->file_local_upload), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_FILE_REMOTE_NAME");
    if (value != NULL) {
        smoke_copy_string(cfg->file_remote_name, sizeof(cfg->file_remote_name), value);
    }
    value = smoke_get_env_nonempty("UDS_SMOKE_FILE_LOCAL_DOWNLOAD");
    if (value != NULL) {
        smoke_copy_string(cfg->file_local_download, sizeof(cfg->file_local_download), value);
    }
    if (smoke_read_u32_env("UDS_SMOKE_FILE_TIMEOUT_MS", &u32_value) && u32_value > 0u) {
        cfg->file_timeout_ms = u32_value;
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
    if (strcmp(key, "run_session_smoke") == 0 || strcmp(key, "session_smoke") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->run_session_smoke = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "session_id") == 0) {
        if (smoke_parse_hex_byte_text(value, &u32_value)) {
            cfg->session_id = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "session_repeat") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->session_repeat = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "session_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->session_timeout_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "session_gap_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->session_gap_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "run_security_smoke") == 0 || strcmp(key, "security_smoke") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->run_security_smoke = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "security_level") == 0) {
        if (smoke_parse_hex_byte_text(value, &u32_value)) {
            cfg->security_level = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "security_repeat") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->security_repeat = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "security_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->security_timeout_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "security_gap_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value)) {
            cfg->security_gap_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "run_rexec_smoke") == 0 || strcmp(key, "rexec_smoke") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->run_rexec_smoke = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "rexec_cmd") == 0) {
        smoke_copy_string(cfg->rexec_cmd, sizeof(cfg->rexec_cmd), value);
        return 1;
    }
    if (strcmp(key, "rexec_expect") == 0) {
        smoke_copy_string(cfg->rexec_expect, sizeof(cfg->rexec_expect), value);
        return 1;
    }
    if (strcmp(key, "rexec_repeat") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->rexec_repeat = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "rexec_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->rexec_timeout_ms = u32_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "run_file_smoke") == 0 || strcmp(key, "file_smoke") == 0) {
        if (smoke_parse_bool_text(value, &bool_value)) {
            cfg->run_file_smoke = bool_value;
            return 1;
        }
        return 0;
    }
    if (strcmp(key, "file_local_upload") == 0) {
        smoke_copy_string(cfg->file_local_upload, sizeof(cfg->file_local_upload), value);
        return 1;
    }
    if (strcmp(key, "file_remote_name") == 0) {
        smoke_copy_string(cfg->file_remote_name, sizeof(cfg->file_remote_name), value);
        return 1;
    }
    if (strcmp(key, "file_local_download") == 0) {
        smoke_copy_string(cfg->file_local_download, sizeof(cfg->file_local_download), value);
        return 1;
    }
    if (strcmp(key, "file_timeout_ms") == 0) {
        if (smoke_parse_u32_text(value, &u32_value) && u32_value > 0u) {
            cfg->file_timeout_ms = u32_value;
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


static void smoke_request_state_reset(smoke_request_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->last_err = UDS_OK;
}

static UDSErr_t smoke_request_event_handler(UDSClient_t *client, UDSEvent_t evt, void *ev_data)
{
    smoke_request_state_t *state;

    if (client == NULL || client->fn_data == NULL) {
        return UDS_OK;
    }

    state = (smoke_request_state_t *)client->fn_data;

    switch (evt) {
    case UDS_EVT_ResponseReceived:
        state->recv_size = client->recv_size;
        if (state->recv_size > sizeof(state->recv_buf)) {
            state->recv_size = (uint16_t)sizeof(state->recv_buf);
        }
        if (state->recv_size > 0u) {
            memcpy(state->recv_buf, client->recv_buf, state->recv_size);
            state->response_sid = state->recv_buf[0];
        }
        if (state->recv_size > 1u) {
            state->response_subfn = state->recv_buf[1];
        }
        if (state->recv_size > 3u) {
            state->response_rid = (uint16_t)(((uint16_t)state->recv_buf[2] << 8) | state->recv_buf[3]);
        }
        state->last_err = UDS_OK;
        state->response_done = 1;
        break;

    case UDS_EVT_Err:
        if (ev_data != NULL) {
            state->last_err = *(const UDSErr_t *)ev_data;
            if (((unsigned)state->last_err & 0xFF00u) == 0u) {
                state->last_nrc = (uint8_t)state->last_err;
            } else {
                state->last_nrc = 0u;
            }
        } else {
            state->last_err = UDS_ERR_TPORT;
            state->last_nrc = 0u;
        }
        state->response_done = 1;
        break;

    default:
        break;
    }

    return UDS_OK;
}

static int smoke_wait_for_request_result(UDSClient_t *client,
                                         smoke_request_state_t *state,
                                         uint32_t timeout_ms)
{
    uint32_t start_ms;

    if (client == NULL || state == NULL || timeout_ms == 0u) {
        return -1;
    }

    start_ms = platform_tick_ms();
    while (!state->response_done) {
        (void)UDSClientPoll(client);
        if ((platform_tick_ms() - start_ms) > timeout_ms) {
            state->last_err = UDS_ERR_TIMEOUT;
            return -1;
        }
        platform_sleep_ms(1u);
    }

    return (state->last_err == UDS_OK) ? 0 : -1;
}

static void smoke_apply_client_timing(UDSClient_t *client, const smoke_client_timing_t *timing)
{
    if (client == NULL || timing == NULL || !timing->valid) {
        return;
    }

    if (timing->p2_ms > 0u) {
        client->p2_ms = timing->p2_ms;
    }
    if (timing->p2_star_ms >= client->p2_ms) {
        client->p2_star_ms = timing->p2_star_ms;
    }
}

static uint32_t smoke_calc_key_from_seed(const uint8_t *seed, uint16_t seed_len)
{
    uint32_t seed_val = 0u;
    uint16_t i;

    for (i = 0u; i < seed_len && i < 4u; ++i) {
        seed_val = (seed_val << 8) | (uint32_t)seed[i];
    }

    return seed_val ^ SMOKE_DEFAULT_SECRET_KEY_MASK;
}

static int smoke_payload_contains(const uint8_t *data, uint16_t len, const char *needle)
{
    size_t needle_len;
    uint16_t i;

    if (needle == NULL || needle[0] == '\0') {
        return 1;
    }
    if (data == NULL || len == 0u) {
        return 0;
    }

    needle_len = strlen(needle);
    if (needle_len == 0u) {
        return 1;
    }
    if ((size_t)len < needle_len) {
        return 0;
    }

    for (i = 0u; (size_t)i + needle_len <= (size_t)len; ++i) {
        if (memcmp(&data[i], needle, needle_len) == 0) {
            return 1;
        }
    }

    return 0;
}

static const char *smoke_file_basename(const char *path)
{
    const char *slash;
    const char *backslash;
    const char *base;

    if (path == NULL || path[0] == '\0') {
        return path;
    }

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    base = path;
    if (slash != NULL && slash[1] != '\0') {
        base = slash + 1;
    }
    if (backslash != NULL && backslash[1] != '\0' && backslash + 1 > base) {
        base = backslash + 1;
    }
    return base;
}

static int smoke_crc32_file(const char *path, uint32_t *crc_out, size_t *size_out)
{
    FILE *fp;
    uint8_t buffer[512];
    size_t nread;
    uint32_t crc = 0u;
    size_t total = 0u;

    if (path == NULL || crc_out == NULL || size_out == NULL) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    while ((nread = fread(buffer, 1u, sizeof(buffer), fp)) > 0u) {
        crc = crc32_calc(crc, buffer, nread);
        total += nread;
    }

    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *crc_out = crc;
    *size_out = total;
    return 0;
}

static int smoke_run_security_sequence(uds_transport_t *tp, const smoke_settings_t *cfg, const smoke_client_timing_t *timing)
{
    UDSClient_t client;
    smoke_request_state_t state;
    struct SecurityAccessResponse resp;
    UDSErr_t err;
    uint32_t attempt;
    uint32_t key_val;
    uint8_t key_bytes[4];

    if (tp == NULL || cfg == NULL) {
        return -1;
    }
    if ((cfg->security_level & 0x1u) == 0u) {
        fprintf(stderr, "[client_smoke] security level must be odd, got 0x%02X\n",
                (unsigned)cfg->security_level);
        return -1;
    }

    memset(&client, 0, sizeof(client));
    smoke_request_state_reset(&state);
    memset(&resp, 0, sizeof(resp));

    err = UDSClientInit(&client);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] security UDSClientInit failed err=%d\n", (int)err);
        return -1;
    }
    client.tp = uds_transport_get_tp_handle(tp);
    client.fn = smoke_request_event_handler;
    client.fn_data = &state;
    smoke_apply_client_timing(&client, timing);
    if (client.tp == NULL) {
        fprintf(stderr, "[client_smoke] security transport handle is NULL\n");
        return -1;
    }

    printf("[client_smoke] security smoke enabled level=0x%02X repeat=%lu timeout_ms=%lu gap_ms=%lu\n",
           (unsigned)cfg->security_level,
           (unsigned long)cfg->security_repeat,
           (unsigned long)cfg->security_timeout_ms,
           (unsigned long)cfg->security_gap_ms);

    for (attempt = 0u; attempt < cfg->security_repeat; ++attempt) {
        smoke_request_state_reset(&state);
        err = UDSSendSecurityAccess(&client, (uint8_t)cfg->security_level, NULL, 0u);
        if (err != UDS_OK) {
            fprintf(stderr, "[client_smoke] security attempt %lu/%lu request-seed send failed err=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->security_repeat,
                    (int)err);
            return -1;
        }
        if (smoke_wait_for_request_result(&client, &state, cfg->security_timeout_ms) != 0) {
            fprintf(stderr, "[client_smoke] security attempt %lu/%lu request-seed failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->security_repeat,
                    (int)state.last_err,
                    state.last_nrc,
                    state.response_sid,
                    uds_transport_get_last_error(tp));
            return -1;
        }
        client.recv_size = state.recv_size;
        if (client.recv_size > 0u) {
            memcpy(client.recv_buf, state.recv_buf, client.recv_size);
        }
        if (UDSUnpackSecurityAccessResponse(&client, &resp) != UDS_OK) {
            fprintf(stderr, "[client_smoke] security attempt %lu/%lu unpack seed response failed\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->security_repeat);
            return -1;
        }
        if (resp.securityAccessType != (uint8_t)cfg->security_level) {
            fprintf(stderr, "[client_smoke] security attempt %lu/%lu response level mismatch got=0x%02X expect=0x%02X\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->security_repeat,
                    (unsigned)resp.securityAccessType,
                    (unsigned)cfg->security_level);
            return -1;
        }
        if (resp.securitySeedLength == 0u) {
            printf("[client_smoke] security attempt %lu/%lu already unlocked level=0x%02X\n",
                   (unsigned long)(attempt + 1u),
                   (unsigned long)cfg->security_repeat,
                   (unsigned)cfg->security_level);
        } else {
            if (resp.securitySeed == NULL || resp.securitySeedLength != 4u) {
                fprintf(stderr, "[client_smoke] security attempt %lu/%lu unexpected seed length=%u\n",
                        (unsigned long)(attempt + 1u),
                        (unsigned long)cfg->security_repeat,
                        (unsigned)resp.securitySeedLength);
                return -1;
            }
            key_val = smoke_calc_key_from_seed(resp.securitySeed, resp.securitySeedLength);
            key_bytes[0] = (uint8_t)((key_val >> 24) & 0xFFu);
            key_bytes[1] = (uint8_t)((key_val >> 16) & 0xFFu);
            key_bytes[2] = (uint8_t)((key_val >> 8) & 0xFFu);
            key_bytes[3] = (uint8_t)(key_val & 0xFFu);

            smoke_request_state_reset(&state);
            err = UDSSendSecurityAccess(&client, (uint8_t)(cfg->security_level + 1u), key_bytes, 4u);
            if (err != UDS_OK) {
                fprintf(stderr, "[client_smoke] security attempt %lu/%lu send-key failed err=%d\n",
                        (unsigned long)(attempt + 1u),
                        (unsigned long)cfg->security_repeat,
                        (int)err);
                return -1;
            }
            if (smoke_wait_for_request_result(&client, &state, cfg->security_timeout_ms) != 0) {
                fprintf(stderr, "[client_smoke] security attempt %lu/%lu send-key failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                        (unsigned long)(attempt + 1u),
                        (unsigned long)cfg->security_repeat,
                        (int)state.last_err,
                        state.last_nrc,
                        state.response_sid,
                        uds_transport_get_last_error(tp));
                return -1;
            }
            if (state.recv_size < 2u || state.response_sid != 0x67u || state.response_subfn != (uint8_t)(cfg->security_level + 1u)) {
                fprintf(stderr, "[client_smoke] security attempt %lu/%lu invalid key response sid=0x%02X sub=0x%02X size=%u\n",
                        (unsigned long)(attempt + 1u),
                        (unsigned long)cfg->security_repeat,
                        state.response_sid,
                        state.response_subfn,
                        (unsigned)state.recv_size);
                return -1;
            }
            printf("[client_smoke] security attempt %lu/%lu ok level=0x%02X key=0x%08lX\n",
                   (unsigned long)(attempt + 1u),
                   (unsigned long)cfg->security_repeat,
                   (unsigned)cfg->security_level,
                   (unsigned long)key_val);
        }

        if (attempt + 1u < cfg->security_repeat && cfg->security_gap_ms > 0u) {
            platform_sleep_ms(cfg->security_gap_ms);
        }
    }

    return 0;
}

static int smoke_run_rexec_sequence(uds_transport_t *tp, const smoke_settings_t *cfg, const smoke_client_timing_t *timing)
{
    UDSClient_t client;
    smoke_request_state_t state;
    UDSErr_t err;
    uint32_t attempt;
    size_t cmd_len;
    uint16_t payload_len;

    if (tp == NULL || cfg == NULL) {
        return -1;
    }
    if (cfg->rexec_cmd[0] == '\0') {
        fprintf(stderr, "[client_smoke] rexec smoke command is empty\n");
        return -1;
    }

    memset(&client, 0, sizeof(client));
    smoke_request_state_reset(&state);
    err = UDSClientInit(&client);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] rexec UDSClientInit failed err=%d\n", (int)err);
        return -1;
    }
    client.tp = uds_transport_get_tp_handle(tp);
    client.fn = smoke_request_event_handler;
    client.fn_data = &state;
    smoke_apply_client_timing(&client, timing);
    if (client.tp == NULL) {
        fprintf(stderr, "[client_smoke] rexec transport handle is NULL\n");
        return -1;
    }

    cmd_len = strlen(cfg->rexec_cmd);
    printf("[client_smoke] rexec smoke enabled repeat=%lu timeout_ms=%lu cmd=%s\n",
           (unsigned long)cfg->rexec_repeat,
           (unsigned long)cfg->rexec_timeout_ms,
           cfg->rexec_cmd);

    for (attempt = 0u; attempt < cfg->rexec_repeat; ++attempt) {
        smoke_request_state_reset(&state);
        err = UDSSendRoutineCtrl(&client,
                                 0x01u,
                                 SMOKE_DEFAULT_REMOTE_CONSOLE_RID,
                                 (const uint8_t *)cfg->rexec_cmd,
                                 (uint16_t)cmd_len);
        if (err != UDS_OK) {
            fprintf(stderr, "[client_smoke] rexec attempt %lu/%lu send failed err=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->rexec_repeat,
                    (int)err);
            return -1;
        }
        if (smoke_wait_for_request_result(&client, &state, cfg->rexec_timeout_ms) != 0) {
            fprintf(stderr, "[client_smoke] rexec attempt %lu/%lu failed err=%d nrc=0x%02X resp_sid=0x%02X resp_sub=0x%02X rid=0x%04X last_error=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->rexec_repeat,
                    (int)state.last_err,
                    state.last_nrc,
                    state.response_sid,
                    state.response_subfn,
                    (unsigned)state.response_rid,
                    uds_transport_get_last_error(tp));
            return -1;
        }
        if (state.recv_size < 4u || state.response_sid != 0x71u || state.response_subfn != 0x01u ||
            state.response_rid != SMOKE_DEFAULT_REMOTE_CONSOLE_RID) {
            fprintf(stderr, "[client_smoke] rexec attempt %lu/%lu invalid response sid=0x%02X sub=0x%02X rid=0x%04X size=%u\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->rexec_repeat,
                    state.response_sid,
                    state.response_subfn,
                    (unsigned)state.response_rid,
                    (unsigned)state.recv_size);
            return -1;
        }
        payload_len = (uint16_t)(state.recv_size - 4u);
        if (!smoke_payload_contains(&state.recv_buf[4], payload_len, cfg->rexec_expect)) {
            fprintf(stderr, "[client_smoke] rexec attempt %lu/%lu output missing expected text: %s\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->rexec_repeat,
                    cfg->rexec_expect);
            return -1;
        }
        printf("[client_smoke] rexec attempt %lu/%lu ok payload_len=%u\n",
               (unsigned long)(attempt + 1u),
               (unsigned long)cfg->rexec_repeat,
               (unsigned)payload_len);
    }

    return 0;
}

static int smoke_run_file_sequence(uds_transport_t *tp, const smoke_settings_t *cfg, const smoke_client_timing_t *timing)
{
    UDSClient_t client;
    smoke_request_state_t state;
    struct RequestFileTransferResponse resp;
    struct stat st;
    FILE *fp;
    const char *remote_name;
    uint8_t buffer[4095];
    uint32_t src_crc = 0u;
    uint32_t dst_crc = 0u;
    size_t src_size = 0u;
    size_t dst_size = 0u;
    size_t max_chunk;
    size_t payload_len;
    size_t sent_bytes;
    size_t received_bytes;
    size_t total_size;
    size_t read_len;
    uint8_t seq;
    uint8_t exit_data[4];
    UDSErr_t err;
    int rc = -1;

    if (tp == NULL || cfg == NULL) {
        return -1;
    }
    if (cfg->file_local_upload[0] == '\0' || cfg->file_local_download[0] == '\0') {
        fprintf(stderr, "[client_smoke] file smoke requires UDS_SMOKE_FILE_LOCAL_UPLOAD and UDS_SMOKE_FILE_LOCAL_DOWNLOAD\n");
        return -1;
    }
    if (stat(cfg->file_local_upload, &st) != 0) {
        fprintf(stderr, "[client_smoke] file smoke upload source not found: %s\n", cfg->file_local_upload);
        return -1;
    }
    remote_name = (cfg->file_remote_name[0] != '\0') ? cfg->file_remote_name : smoke_file_basename(cfg->file_local_upload);
    if (remote_name == NULL || remote_name[0] == '\0') {
        fprintf(stderr, "[client_smoke] file smoke remote name is empty\n");
        return -1;
    }
    if (smoke_crc32_file(cfg->file_local_upload, &src_crc, &src_size) != 0) {
        fprintf(stderr, "[client_smoke] file smoke failed to compute source CRC: %s\n", cfg->file_local_upload);
        return -1;
    }

    memset(&client, 0, sizeof(client));
    smoke_request_state_reset(&state);
    memset(&resp, 0, sizeof(resp));
    err = UDSClientInit(&client);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] file UDSClientInit failed err=%d\n", (int)err);
        return -1;
    }
    client.tp = uds_transport_get_tp_handle(tp);
    client.fn = smoke_request_event_handler;
    client.fn_data = &state;
    smoke_apply_client_timing(&client, timing);
    if (client.tp == NULL) {
        fprintf(stderr, "[client_smoke] file transport handle is NULL\n");
        return -1;
    }

    printf("[client_smoke] file smoke enabled upload=%s remote=%s download=%s timeout_ms=%lu\n",
           cfg->file_local_upload,
           remote_name,
           cfg->file_local_download,
           (unsigned long)cfg->file_timeout_ms);

    fp = fopen(cfg->file_local_upload, "rb");
    if (fp == NULL) {
        fprintf(stderr, "[client_smoke] file smoke failed to open upload source: %s\n", cfg->file_local_upload);
        return -1;
    }

    smoke_request_state_reset(&state);
    err = UDSSendRequestFileTransfer(&client,
                                     SMOKE_DEFAULT_MOOP_ADD_FILE,
                                     remote_name,
                                     0x00u,
                                     4u,
                                     src_size,
                                     src_size);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] file upload init send failed err=%d\n", (int)err);
        goto cleanup;
    }
    if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0) {
        fprintf(stderr, "[client_smoke] file upload init failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                (int)state.last_err,
                state.last_nrc,
                state.response_sid,
                uds_transport_get_last_error(tp));
        goto cleanup;
    }
    client.recv_size = state.recv_size;
    if (client.recv_size > 0u) {
        memcpy(client.recv_buf, state.recv_buf, client.recv_size);
    }
    if (UDSUnpackRequestFileTransferResponse(&client, &resp) != UDS_OK) {
        fprintf(stderr, "[client_smoke] file upload init unpack failed\n");
        goto cleanup;
    }
    max_chunk = resp.maxNumberOfBlockLength;
    if (max_chunk < 3u) {
        fprintf(stderr, "[client_smoke] file upload invalid maxNumberOfBlockLength=%lu\n",
                (unsigned long)max_chunk);
        goto cleanup;
    }
    if (max_chunk > (sizeof(buffer) + 2u)) {
        max_chunk = sizeof(buffer) + 2u;
    }
    payload_len = max_chunk - 2u;
    sent_bytes = 0u;
    seq = 1u;
    while (sent_bytes < src_size) {
        read_len = fread(buffer, 1u, payload_len, fp);
        if (read_len == 0u) {
            if (ferror(fp)) {
                fprintf(stderr, "[client_smoke] file upload read failed after %lu bytes\n",
                        (unsigned long)sent_bytes);
                goto cleanup;
            }
            break;
        }
        smoke_request_state_reset(&state);
        err = UDSSendTransferData(&client, seq, (uint16_t)(read_len + 2u), buffer, (uint16_t)read_len);
        if (err != UDS_OK) {
            fprintf(stderr, "[client_smoke] file upload block %u send failed err=%d\n",
                    (unsigned)seq,
                    (int)err);
            goto cleanup;
        }
        if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0) {
            fprintf(stderr, "[client_smoke] file upload block %u failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                    (unsigned)seq,
                    (int)state.last_err,
                    state.last_nrc,
                    state.response_sid,
                    uds_transport_get_last_error(tp));
            goto cleanup;
        }
        if (state.recv_size < 2u || state.response_sid != 0x76u || state.response_subfn != seq) {
            fprintf(stderr, "[client_smoke] file upload block %u invalid response sid=0x%02X seq=0x%02X size=%u\n",
                    (unsigned)seq,
                    state.response_sid,
                    state.response_subfn,
                    (unsigned)state.recv_size);
            goto cleanup;
        }
        sent_bytes += read_len;
        ++seq;
    }
    fclose(fp);
    fp = NULL;

    exit_data[0] = (uint8_t)((src_crc >> 24) & 0xFFu);
    exit_data[1] = (uint8_t)((src_crc >> 16) & 0xFFu);
    exit_data[2] = (uint8_t)((src_crc >> 8) & 0xFFu);
    exit_data[3] = (uint8_t)(src_crc & 0xFFu);
    smoke_request_state_reset(&state);
    err = UDSSendRequestTransferExit(&client, exit_data, 4u);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] file upload exit send failed err=%d\n", (int)err);
        goto cleanup;
    }
    if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0 || state.response_sid != 0x77u) {
        fprintf(stderr, "[client_smoke] file upload exit failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                (int)state.last_err,
                state.last_nrc,
                state.response_sid,
                uds_transport_get_last_error(tp));
        goto cleanup;
    }
    printf("[client_smoke] file upload ok size=%lu crc=0x%08lX\n",
           (unsigned long)src_size,
           (unsigned long)src_crc);

    fp = fopen(cfg->file_local_download, "wb");
    if (fp == NULL) {
        fprintf(stderr, "[client_smoke] file smoke failed to open download target: %s\n", cfg->file_local_download);
        goto cleanup;
    }

    smoke_request_state_reset(&state);
    err = UDSSendRequestFileTransfer(&client,
                                     SMOKE_DEFAULT_MOOP_READ_FILE,
                                     remote_name,
                                     0x00u,
                                     0u,
                                     0u,
                                     0u);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] file download init send failed err=%d\n", (int)err);
        goto cleanup;
    }
    if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0) {
        fprintf(stderr, "[client_smoke] file download init failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                (int)state.last_err,
                state.last_nrc,
                state.response_sid,
                uds_transport_get_last_error(tp));
        goto cleanup;
    }
    client.recv_size = state.recv_size;
    if (client.recv_size > 0u) {
        memcpy(client.recv_buf, state.recv_buf, client.recv_size);
    }
    if (UDSUnpackRequestFileTransferResponse(&client, &resp) != UDS_OK) {
        fprintf(stderr, "[client_smoke] file download init unpack failed\n");
        goto cleanup;
    }
    total_size = resp.fileSizeUncompressed;
    received_bytes = 0u;
    dst_crc = 0u;
    seq = 1u;

    for (;;) {
        size_t data_len;

        smoke_request_state_reset(&state);
        err = UDSSendTransferData(&client, seq, 2u, NULL, 0u);
        if (err != UDS_OK) {
            fprintf(stderr, "[client_smoke] file download block %u send failed err=%d\n",
                    (unsigned)seq,
                    (int)err);
            goto cleanup;
        }
        if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0) {
            fprintf(stderr, "[client_smoke] file download block %u failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                    (unsigned)seq,
                    (int)state.last_err,
                    state.last_nrc,
                    state.response_sid,
                    uds_transport_get_last_error(tp));
            goto cleanup;
        }
        if (state.recv_size < 2u || state.response_sid != 0x76u || state.response_subfn != seq) {
            fprintf(stderr, "[client_smoke] file download block %u invalid response sid=0x%02X seq=0x%02X size=%u\n",
                    (unsigned)seq,
                    state.response_sid,
                    state.response_subfn,
                    (unsigned)state.recv_size);
            goto cleanup;
        }
        data_len = (size_t)state.recv_size - 2u;
        if (data_len == 0u) {
            break;
        }
        if (fwrite(&state.recv_buf[2], 1u, data_len, fp) != data_len) {
            fprintf(stderr, "[client_smoke] file download write failed after %lu bytes\n",
                    (unsigned long)received_bytes);
            goto cleanup;
        }
        dst_crc = crc32_calc(dst_crc, &state.recv_buf[2], data_len);
        received_bytes += data_len;
        ++seq;
        if (total_size > 0u && received_bytes >= total_size) {
            break;
        }
    }

    smoke_request_state_reset(&state);
    err = UDSSendRequestTransferExit(&client, NULL, 0u);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] file download exit send failed err=%d\n", (int)err);
        goto cleanup;
    }
    if (smoke_wait_for_request_result(&client, &state, cfg->file_timeout_ms) != 0 || state.response_sid != 0x77u) {
        fprintf(stderr, "[client_smoke] file download exit failed err=%d nrc=0x%02X resp_sid=0x%02X last_error=%d\n",
                (int)state.last_err,
                state.last_nrc,
                state.response_sid,
                uds_transport_get_last_error(tp));
        goto cleanup;
    }
    fclose(fp);
    fp = NULL;

    if (smoke_crc32_file(cfg->file_local_download, &dst_crc, &dst_size) != 0) {
        fprintf(stderr, "[client_smoke] file smoke failed to compute download CRC: %s\n", cfg->file_local_download);
        goto cleanup;
    }
    if (dst_size != src_size || dst_crc != src_crc) {
        fprintf(stderr, "[client_smoke] file smoke compare mismatch src_size=%lu dst_size=%lu src_crc=0x%08lX dst_crc=0x%08lX\n",
                (unsigned long)src_size,
                (unsigned long)dst_size,
                (unsigned long)src_crc,
                (unsigned long)dst_crc);
        goto cleanup;
    }

    printf("[client_smoke] file download ok size=%lu crc=0x%08lX\n",
           (unsigned long)dst_size,
           (unsigned long)dst_crc);
    rc = 0;

cleanup:
    if (fp != NULL) {
        fclose(fp);
    }
    if (rc != 0) {
        remove(cfg->file_local_download);
    }
    return rc;
}

static void smoke_session_state_reset(smoke_session_state_t *state, uint8_t expected_session)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->expected_session = expected_session;
    state->last_err = UDS_OK;
}

static UDSErr_t smoke_session_event_handler(UDSClient_t *client, UDSEvent_t evt, void *ev_data)
{
    smoke_session_state_t *state;

    if (client == NULL || client->fn_data == NULL) {
        return UDS_OK;
    }

    state = (smoke_session_state_t *)client->fn_data;

    switch (evt) {
    case UDS_EVT_ResponseReceived:
        if (client->recv_size < 2u) {
            state->last_err = UDS_ERR_RESP_TOO_SHORT;
        } else {
            state->response_sid = client->recv_buf[0];
            state->response_subfn = client->recv_buf[1];

            if (state->response_sid != 0x50u) {
                state->last_err = UDS_ERR_SID_MISMATCH;
            } else if (state->response_subfn != state->expected_session) {
                state->last_err = UDS_ERR_SUBFUNCTION_MISMATCH;
            } else {
                state->last_err = UDS_OK;
            }
        }
        state->response_done = 1;
        break;

    case UDS_EVT_Err:
        if (ev_data != NULL) {
            state->last_err = *(const UDSErr_t *)ev_data;
            if (((unsigned)state->last_err & 0xFF00u) == 0u) {
                state->last_nrc = (uint8_t)state->last_err;
            } else {
                state->last_nrc = 0u;
            }
        } else {
            state->last_err = UDS_ERR_TPORT;
        }
        state->response_done = 1;
        break;

    default:
        break;
    }

    return UDS_OK;
}

static int smoke_wait_for_session_result(UDSClient_t *client, smoke_session_state_t *state, uint32_t timeout_ms)
{
    uint32_t start_ms;

    if (client == NULL || state == NULL || timeout_ms == 0u) {
        return -1;
    }

    start_ms = platform_tick_ms();
    while (!state->response_done) {
        (void)UDSClientPoll(client);
        if (state->response_done) {
            break;
        }
        if ((platform_tick_ms() - start_ms) > timeout_ms) {
            state->last_err = UDS_ERR_TIMEOUT;
            return -1;
        }
        platform_sleep_ms(1u);
    }

    return (state->last_err == UDS_OK) ? 0 : -1;
}

static int smoke_run_session_sequence(uds_transport_t *tp, const smoke_settings_t *cfg, smoke_client_timing_t *timing)
{
    UDSClient_t client;
    smoke_session_state_t state;
    UDSErr_t err;
    uint32_t attempt;

    if (tp == NULL || cfg == NULL) {
        return -1;
    }

    memset(&client, 0, sizeof(client));
    smoke_session_state_reset(&state, (uint8_t)cfg->session_id);

    err = UDSClientInit(&client);
    if (err != UDS_OK) {
        fprintf(stderr, "[client_smoke] UDSClientInit failed err=%d\n", (int)err);
        return -1;
    }

    client.tp = uds_transport_get_tp_handle(tp);
    client.fn = smoke_session_event_handler;
    client.fn_data = &state;

    if (client.tp == NULL) {
        fprintf(stderr, "[client_smoke] transport handle is NULL\n");
        return -1;
    }

    printf("[client_smoke] session smoke enabled sid=0x%02X repeat=%lu timeout_ms=%lu gap_ms=%lu\n",
           (unsigned)cfg->session_id,
           (unsigned long)cfg->session_repeat,
           (unsigned long)cfg->session_timeout_ms,
           (unsigned long)cfg->session_gap_ms);

    for (attempt = 0u; attempt < cfg->session_repeat; ++attempt) {
        smoke_session_state_reset(&state, (uint8_t)cfg->session_id);
        err = UDSSendDiagSessCtrl(&client, (uint8_t)cfg->session_id);
        if (err != UDS_OK) {
            fprintf(stderr,
                    "[client_smoke] session attempt %lu/%lu send failed err=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->session_repeat,
                    (int)err);
            return -1;
        }

        if (smoke_wait_for_session_result(&client, &state, cfg->session_timeout_ms) != 0) {
            fprintf(stderr,
                    "[client_smoke] session attempt %lu/%lu failed err=%d nrc=0x%02X resp_sid=0x%02X resp_sub=0x%02X last_error=%d\n",
                    (unsigned long)(attempt + 1u),
                    (unsigned long)cfg->session_repeat,
                    (int)state.last_err,
                    state.last_nrc,
                    state.response_sid,
                    state.response_subfn,
                    uds_transport_get_last_error(tp));
            return -1;
        }
        if (timing != NULL) {
            timing->valid = 1;
            timing->p2_ms = client.p2_ms;
            timing->p2_star_ms = client.p2_star_ms;
        }

        printf("[client_smoke] session attempt %lu/%lu ok sid=0x%02X resp_sid=0x%02X resp_sub=0x%02X p2=%lu p2*=%lu\n",
               (unsigned long)(attempt + 1u),
               (unsigned long)cfg->session_repeat,
               (unsigned)cfg->session_id,
               state.response_sid,
               state.response_subfn,
               (unsigned long)client.p2_ms,
               (unsigned long)client.p2_star_ms);

        if (attempt + 1u < cfg->session_repeat && cfg->session_gap_ms > 0u) {
            platform_sleep_ms(cfg->session_gap_ms);
        }
    }

    return 0;
}

int main(void)
{
    uds_transport_t tp;
    unsigned char storage[UDS_TRANSPORT_STORAGE_CAPACITY];
    uds_transport_open_cfg_t open_cfg;
    smoke_settings_t smoke_cfg;
    smoke_client_timing_t timing;
    int rc;

    smoke_settings_init_defaults(&smoke_cfg);
    smoke_try_load_default_conf(&smoke_cfg);
    smoke_settings_apply_env(&smoke_cfg);
    memset(&timing, 0, sizeof(timing));

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

    if (rc == 0 && smoke_cfg.run_session_smoke) {
        rc = smoke_run_session_sequence(&tp, &smoke_cfg, &timing);
    }
    if (rc == 0 && smoke_cfg.run_security_smoke) {
        rc = smoke_run_security_sequence(&tp, &smoke_cfg, &timing);
    }
    if (rc == 0 && smoke_cfg.run_rexec_smoke) {
        rc = smoke_run_rexec_sequence(&tp, &smoke_cfg, &timing);
    }
    if (rc == 0 && smoke_cfg.run_file_smoke) {
        rc = smoke_run_file_sequence(&tp, &smoke_cfg, &timing);
    }

    uds_transport_close(&tp);
    printf("[client_smoke] transport closed cleanly\n");
    return (rc == 0) ? 0 : 3;
}

