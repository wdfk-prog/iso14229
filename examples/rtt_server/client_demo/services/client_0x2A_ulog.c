/**
 * @file client_0x2A_ulog.c
 * @brief Service 0x2A (ReadDataByPeriodicIdentifier) ULOG client helper.
 * @details Provides:
 *          - `ulog2a on [slow|med|fast] [pdid_hex]`
 *          - `ulog2a off`
 *          - automatic start/stop helpers for main lifecycle
 *          - unsolicited payload parsing and raw log streaming output
 */
#define LOG_TAG "ULOG2A"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/uds_context.h"
#include "../core/client_shell.h"
#include "../utils/utils.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CLIENT_ULOG2A_DEFAULT_PDID
#define CLIENT_ULOG2A_DEFAULT_PDID 0xA1
#endif

/**
 * @brief Keep runtime state for 0x2A ULOG streaming.
 */
typedef struct {
    int enabled;                     /**< Stream enable flag: 1 enabled, 0 disabled. */
    uint8_t pdid;                    /**< Active periodic data identifier (PDID). */
    UDSRDBPITransmissionMode_t mode; /**< Active periodic transmission mode. */
} client_ulog2a_state_t;

/**
 * @brief Hold the singleton state of the 0x2A ULOG client helper.
 */
static client_ulog2a_state_t g_ulog2a = {
    .enabled = 0,
    .pdid = CLIENT_ULOG2A_DEFAULT_PDID,
    .mode = UDS_TM_SEND_AT_MEDIUM_RATE,
};

/**
 * @brief Compare two C strings using case-insensitive ASCII rules.
 * @param a First string pointer.
 * @param b Second string pointer.
 * @return 1 when both strings are equal, otherwise 0.
 */
static int str_eq_nocase(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/**
 * @brief Convert a transmission mode enum to a display label.
 * @param mode Transmission mode value.
 * @return Pointer to static mode name string.
 */
static const char *mode_to_name(UDSRDBPITransmissionMode_t mode)
{
    switch (mode) {
        case UDS_TM_SEND_AT_SLOW_RATE:
            return "slow";
        case UDS_TM_SEND_AT_MEDIUM_RATE:
            return "medium";
        case UDS_TM_SEND_AT_FAST_RATE:
            return "fast";
        default:
            return "unknown";
    }
}

/**
 * @brief Parse one textual mode token into a 0x2A transmission mode.
 * @param token Mode token such as "slow", "med", "medium", or "fast".
 * @param mode Output pointer receiving parsed transmission mode.
 * @return 0 on success, -1 on parse failure.
 */
static int parse_mode(const char *token, UDSRDBPITransmissionMode_t *mode)
{
    if (token == NULL || mode == NULL) {
        return -1;
    }

    if (str_eq_nocase(token, "slow")) {
        *mode = UDS_TM_SEND_AT_SLOW_RATE;
        return 0;
    }
    if (str_eq_nocase(token, "med") || str_eq_nocase(token, "medium")) {
        *mode = UDS_TM_SEND_AT_MEDIUM_RATE;
        return 0;
    }
    if (str_eq_nocase(token, "fast")) {
        *mode = UDS_TM_SEND_AT_FAST_RATE;
        return 0;
    }
    return -1;
}

/**
 * @brief Parse one PDID token from hexadecimal text.
 * @param token Hexadecimal PDID token.
 * @param pdid Output pointer receiving parsed PDID value.
 * @return 0 on success, -1 on parse failure.
 */
static int parse_pdid(const char *token, uint8_t *pdid)
{
    char *end = NULL;
    unsigned long value;

    if (token == NULL || pdid == NULL) {
        return -1;
    }

    errno = 0;
    value = strtoul(token, &end, 16);
    if (errno != 0 || end == token || *end != '\0' || value > 0xFFUL) {
        return -1;
    }

    *pdid = (uint8_t)value;
    return 0;
}

/**
 * @brief Print command usage for the ulog2a local command.
 */
static void print_usage(void)
{
    printf("Usage:\n");
    printf("  ulog2a on [slow|med|fast] [pdid_hex]\n");
    printf("  ulog2a off\n");
    printf("Defaults: mode=med, pdid=0x%02X\n", CLIENT_ULOG2A_DEFAULT_PDID);
}

/**
 * @brief Start periodic ULOG streaming by sending a 0x2A subscription request.
 * @param mode Requested transmission mode.
 * @param pdid Requested periodic data identifier.
 * @param wait_msg Spinner label shown while waiting for the response.
 * @return 0 on success, -1 on failure.
 */
static int ulog2a_start(UDSRDBPITransmissionMode_t mode, uint8_t pdid, const char *wait_msg)
{
    uint8_t list[1] = { pdid };

    if (UDS_TRANSACTION(UDSSendRDBPI(uds_get_client(), mode, list, 1), wait_msg) != 0) {
        return -1;
    }

    g_ulog2a.enabled = 1;
    g_ulog2a.mode = mode;
    g_ulog2a.pdid = pdid;
    LOG_INFO("Enabled (mode=%s, pdid=0x%02X)", mode_to_name(mode), pdid);
    return 0;
}

/**
 * @brief Stop periodic streaming by sending a 0x2A stop-all request.
 * @param wait_msg Spinner label shown while waiting for the response.
 * @return 0 on success, -1 on failure.
 */
static int ulog2a_stop(const char *wait_msg)
{
    if (UDS_TRANSACTION(UDSSendRDBPI(uds_get_client(), UDS_TM_STOP_SENDING, NULL, 0), wait_msg) != 0) {
        return -1;
    }

    g_ulog2a.enabled = 0;
    LOG_INFO("Disabled (stop-all)");
    return 0;
}

/**
 * @brief Write a byte stream to the shell while converting LF to CRLF.
 * @param src Source byte stream.
 * @param len Number of bytes in @p src.
 */
static void ulog2a_write_with_crlf(const uint8_t *src, size_t len)
{
    uint8_t out[256];
    size_t out_len = 0;

    if (src == NULL || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t ch = src[i];

        if (ch == '\n') {
            if (out_len + 2 > sizeof(out)) {
                client_shell_async_write(out, out_len);
                out_len = 0;
            }
            out[out_len++] = '\r';
            out[out_len++] = '\n';
        } else {
            if (out_len + 1 > sizeof(out)) {
                client_shell_async_write(out, out_len);
                out_len = 0;
            }
            out[out_len++] = ch;
        }
    }

    if (out_len > 0) {
        client_shell_async_write(out, out_len);
    }
}

/**
 * @brief Handle unsolicited payloads and print matching 0x2A ULOG data.
 * @details Payload format is expected to be:
 *          - byte 0: PDID
 *          - byte 1..N: text payload
 * @param args Unsolicited payload event data from UDS context.
 */
static void handle_ulog2a_unsolicited(const UDSClientPayloadArgs_t *args)
{
    const uint8_t *payload;
    uint16_t len;
    uint8_t pdid;

    if (args == NULL || args->payload.data == NULL) {
        return;
    }
    if (!g_ulog2a.enabled) {
        return;
    }

    payload = args->payload.data;
    len = args->payload.len;
    if (len < 2U) {
        return;
    }

    pdid = payload[0];
    if (pdid != g_ulog2a.pdid) {
        return;
    }

    ulog2a_write_with_crlf(&payload[1], (size_t)(len - 1U));
}

/**
 * @brief Handle the local `ulog2a` command.
 * @param argc Number of command arguments.
 * @param argv Command argument array.
 * @return 0 on success or usage-only path, -1 on failure.
 */
static int handle_ulog2a_cmd(int argc, char **argv)
{
    UDSRDBPITransmissionMode_t mode = UDS_TM_SEND_AT_MEDIUM_RATE;
    uint8_t pdid = CLIENT_ULOG2A_DEFAULT_PDID;

    if (argc < 2) {
        print_usage();
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        if (argc > 4) {
            print_usage();
            return 0;
        }

        if (argc >= 3 && parse_mode(argv[2], &mode) != 0) {
            LOG_ERROR("Invalid mode '%s'", argv[2]);
            print_usage();
            return -1;
        }

        if (argc >= 4 && parse_pdid(argv[3], &pdid) != 0) {
            LOG_ERROR("Invalid PDID '%s'", argv[3]);
            print_usage();
            return -1;
        }

        return ulog2a_start(mode, pdid, "Enabling 0x2A ULOG");
    }

    if (strcmp(argv[1], "off") == 0) {
        if (argc != 2) {
            print_usage();
            return 0;
        }
        return ulog2a_stop("Disabling 0x2A ULOG");
    }

    print_usage();
    return 0;
}

/**
 * @brief Auto-enable 0x2A ULOG streaming with current stored defaults.
 * @return 0 on success, -1 on failure.
 */
int client_0x2A_ulog_auto_start(void)
{
    if (g_ulog2a.enabled) {
        return 0;
    }

    return ulog2a_start(g_ulog2a.mode, g_ulog2a.pdid, "Auto enabling 0x2A ULOG");
}

/**
 * @brief Auto-disable 0x2A ULOG streaming by issuing stop-all.
 * @return 0 on success, -1 on failure.
 */
int client_0x2A_ulog_auto_stop(void)
{
    if (!g_ulog2a.enabled) {
        return 0;
    }

    return ulog2a_stop("Auto disabling 0x2A ULOG");
}

/**
 * @brief Initialize the 0x2A ULOG helper module.
 * @details Resets local state, registers the local command, and installs the
 *          unsolicited payload callback.
 */
void client_0x2A_init(void)
{
    g_ulog2a.enabled = 0;
    g_ulog2a.pdid = CLIENT_ULOG2A_DEFAULT_PDID;
    g_ulog2a.mode = UDS_TM_SEND_AT_MEDIUM_RATE;

    cmd_register("ulog2a",
                 handle_ulog2a_cmd,
                 "0x2A ULOG stream on/off",
                 " <on|off> [slow|med|fast] [pdid]");

    uds_register_unsolicited_payload_callback(handle_ulog2a_unsolicited);
}
