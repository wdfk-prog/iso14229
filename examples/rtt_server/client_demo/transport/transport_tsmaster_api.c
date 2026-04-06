/**
 * @file transport_tsmaster_api.c
 * @brief Windows TSMaster API backend for the transport abstraction.
 *
 * Design notes:
 * - This backend reuses iso14229's `UDSISOTpC_t` implementation so callback
 *   threads only copy raw CAN/CAN-FD frames into a small ring buffer.
 * - The actual ISO-TP reassembly work happens from the UDS polling path
 *   (`poll -> drain queue -> isotp_on_can_message -> UDSISOTpC poll`).
 * - To keep the current task boundary, this backend does not change upstream
 *   `iso14229` behaviour.
 * - Current `iso14229` in this repository processes ISO-TP CAN frames with
 *   payload lengths up to 8 bytes. Therefore CAN FD transport is supported at
 *   the SDK frame/API level, but ISO-TP segmentation still follows classical
 *   8-byte CAN framing semantics.
 */

#include "transport.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef UDS_TSMASTER_DLL_HINT_PATH
#define UDS_TSMASTER_DLL_HINT_PATH NULL
#endif

#ifndef UDS_TSMASTER_DLL_HINT_PATH_ALT
#define UDS_TSMASTER_DLL_HINT_PATH_ALT NULL
#endif

#if defined(__has_include)
#  if __has_include("TSMaster.h")
#    include "TSMaster.h"
#    define UDS_TSMASTER_VENDOR_HEADER 1
#  endif
#endif

#ifndef UDS_TSMASTER_VENDOR_HEADER
/*
 * Minimal fallback declarations derived from public SDK examples / docs.
 * They are only used when the vendor header is unavailable to the compiler.
 */
typedef struct {
    uint8_t FIdxChn;
    uint8_t FProperties;
    uint8_t FDLC;
    uint8_t FReserved;
    int32_t FIdentifier;
    int64_t FTimeUs;
    uint8_t FData[8];
} TLIBCAN;
typedef TLIBCAN *PLIBCAN;
typedef TLIBCAN *PCAN;

typedef struct {
    uint8_t FIdxChn;
    uint8_t FProperties;
    uint8_t FDLC;
    uint8_t FFDProperties;
    int32_t FIdentifier;
    uint64_t FTimeUs;
    uint8_t FData[64];
} TLIBCANFD;
typedef TLIBCANFD *PLIBCANFD;
typedef TLIBCANFD *PCANFD;

typedef struct {
    char FAppName[32];
    char FHWDeviceName[32];
    int32_t FAppChannelIndex;
    int32_t FAppChannelType;
    int32_t FHWDeviceType;
    int32_t FHWDeviceSubType;
    int32_t FHWIndex;
    int32_t FHWChannelIndex;
    int32_t FMappingDisabled;
} TLIBTSMapping;
typedef TLIBTSMapping *PLIBTSMapping;
#endif

#ifndef UDS_TP_ISOTP_C
#error "TSMaster backend requires UDS_TP_ISOTP_C to be enabled for Windows builds"
#endif

#ifndef UDS_TSMASTER_APP_CAN
#define UDS_TSMASTER_APP_CAN 0
#endif

#ifndef UDS_TSMASTER_FD_CONTROLLER_ISO
#define UDS_TSMASTER_FD_CONTROLLER_ISO 1
#endif

#ifndef UDS_TSMASTER_FD_MODE_NORMAL
#define UDS_TSMASTER_FD_MODE_NORMAL 0
#endif

#define UDS_TSMASTER_DEFAULT_APP_NAME         "UDSClient"
#define UDS_TSMASTER_DEFAULT_HW_NAME          "TSMaster"
#define UDS_TSMASTER_DEFAULT_CAN_BAUD_KBPS    (500.0)
#define UDS_TSMASTER_DEFAULT_CANFD_ARB_KBPS   (500.0)
#define UDS_TSMASTER_DEFAULT_CANFD_DATA_KBPS  (2000.0)
#define UDS_TSMASTER_RX_QUEUE_LEN             64U
#define UDS_TSMASTER_CALLBACK_TAG             ((intptr_t)0x55445331) /* 'UDS1' */

#define UDS_TSMASTER_CAN_PROP_TX              0x01U
#define UDS_TSMASTER_CAN_PROP_REMOTE          0x02U
#define UDS_TSMASTER_CAN_PROP_EXT             0x04U
#define UDS_TSMASTER_CANFD_PROP_EDL           0x01U
#define UDS_TSMASTER_CANFD_PROP_BRS           0x02U

#define UDS_TSMASTER_ID_MASK_STD              0x7FFU
#define UDS_TSMASTER_ID_MASK_EXT              0x1FFFFFFFU

typedef intptr_t uds_tsmaster_callback_obj_t;

typedef void(__stdcall *uds_tsmaster_can_event_cb_t)(const uds_tsmaster_callback_obj_t obj,
                                                     const PLIBCAN msg);
typedef void(__stdcall *uds_tsmaster_canfd_event_cb_t)(const uds_tsmaster_callback_obj_t obj,
                                                       const PLIBCANFD msg);

typedef int (*uds_tsmaster_initialize_lib_tsmaster_fn)(const char *app_name);
typedef void (*uds_tsmaster_finalize_lib_tsmaster_fn)(void);
typedef int (*uds_tsmaster_set_current_application_fn)(const char *app_name);
typedef int (*uds_tsmaster_set_can_channel_count_fn)(int32_t channel_count);
typedef int (*uds_tsmaster_set_mapping_fn)(const PLIBTSMapping mapping);
typedef int (*uds_tsmaster_get_mapping_fn)(PLIBTSMapping mapping);
typedef int (*uds_tsmaster_configure_baudrate_can_fn)(int32_t channel_index,
                                                       float baudrate_kbps,
                                                       int32_t install_term_resistor);
typedef int (*uds_tsmaster_configure_baudrate_canfd_fn)(int32_t channel_index,
                                                         float arb_baudrate_kbps,
                                                         float data_baudrate_kbps,
                                                         int32_t controller_type,
                                                         int32_t controller_mode,
                                                         int32_t install_term_resistor);
typedef int (*uds_tsmaster_connect_fn)(void);
typedef int (*uds_tsmaster_disconnect_fn)(void);
typedef int (*uds_tsmaster_register_event_can_fn)(uds_tsmaster_callback_obj_t obj,
                                                   uds_tsmaster_can_event_cb_t cb);
typedef int (*uds_tsmaster_unregister_event_can_fn)(uds_tsmaster_callback_obj_t obj,
                                                     uds_tsmaster_can_event_cb_t cb);
typedef int (*uds_tsmaster_register_event_canfd_fn)(uds_tsmaster_callback_obj_t obj,
                                                     uds_tsmaster_canfd_event_cb_t cb);
typedef int (*uds_tsmaster_unregister_event_canfd_fn)(uds_tsmaster_callback_obj_t obj,
                                                       uds_tsmaster_canfd_event_cb_t cb);
typedef int (*uds_tsmaster_transmit_can_async_fn)(PLIBCAN msg);
typedef int (*uds_tsmaster_transmit_canfd_async_fn)(PLIBCANFD msg);

typedef struct {
    uint32_t can_id;
    uint8_t data[64];
    uint8_t len;
    uint8_t is_fd;
} uds_tsmaster_rx_frame_t;

typedef struct {
    UDSISOTpC_t isotp;
    UDSTpStatus_t (*original_poll)(struct UDSTp *hdl);
    uds_transport_t *owner;

    CRITICAL_SECTION queue_lock;
    bool queue_lock_initialized;
    uds_tsmaster_rx_frame_t rx_queue[UDS_TSMASTER_RX_QUEUE_LEN];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
    bool rx_overflow;
    bool unsupported_fd_len_seen;

    HMODULE sdk_module;
    bool sdk_module_owned;
    DWORD last_win32_error;
    const char *last_open_stage;
    bool sdk_initialized;
    bool connected;
    bool callbacks_registered;
    bool use_canfd;
    bool use_brs;
    bool install_term_resistor;
    bool use_extended_ids;

    int32_t app_channel_index;
    uint32_t phys_rx_can_id;
    uint32_t phys_tx_can_id;
    uint32_t func_tx_can_id;
    uds_tsmaster_callback_obj_t callback_tag;
    int last_sdk_error;

    char app_name[32];
    char hw_name[32];
    int32_t hw_device_type;
    int32_t hw_device_sub_type;
    int32_t hw_index;
    int32_t hw_channel_index;
    float can_baudrate_kbps;
    float canfd_arb_baudrate_kbps;
    float canfd_data_baudrate_kbps;

    uds_tsmaster_initialize_lib_tsmaster_fn initialize_lib_tsmaster;
    uds_tsmaster_finalize_lib_tsmaster_fn finalize_lib_tsmaster;
    uds_tsmaster_set_current_application_fn tsapp_set_current_application;
    uds_tsmaster_set_can_channel_count_fn tsapp_set_can_channel_count;
    uds_tsmaster_set_mapping_fn tsapp_set_mapping;
    uds_tsmaster_get_mapping_fn tsapp_get_mapping;
    uds_tsmaster_configure_baudrate_can_fn tsapp_configure_baudrate_can;
    uds_tsmaster_configure_baudrate_canfd_fn tsapp_configure_baudrate_canfd;
    uds_tsmaster_connect_fn tsapp_connect;
    uds_tsmaster_disconnect_fn tsapp_disconnect;
    uds_tsmaster_register_event_can_fn tsapp_register_event_can;
    uds_tsmaster_unregister_event_can_fn tsapp_unregister_event_can;
    uds_tsmaster_register_event_canfd_fn tsapp_register_event_canfd;
    uds_tsmaster_unregister_event_canfd_fn tsapp_unregister_event_canfd;
    uds_tsmaster_transmit_can_async_fn tsapp_transmit_can_async;
    uds_tsmaster_transmit_canfd_async_fn tsapp_transmit_canfd_async;
} uds_transport_tsmaster_ctx_t;

_Static_assert(offsetof(uds_transport_tsmaster_ctx_t, isotp) == 0,
               "uds_transport_tsmaster_ctx_t.isotp must be at offset 0");
_Static_assert(offsetof(UDSISOTpC_t, hdl) == 0,
               "UDSISOTpC_t.hdl must be at offset 0");
_Static_assert(sizeof(uds_transport_tsmaster_ctx_t) <= UDS_TRANSPORT_STORAGE_CAPACITY,
               "UDS_TRANSPORT_STORAGE_CAPACITY is too small for TSMaster backend context");

static uds_transport_tsmaster_ctx_t *g_tsmaster_active_ctx = NULL;

static uds_transport_tsmaster_ctx_t *tsmaster_ctx(uds_transport_t *tp)
{
    return (uds_transport_tsmaster_ctx_t *)tp->backend_ctx;
}

static uint8_t tsmaster_canfd_dlc_to_len(uint8_t dlc)
{
    static const uint8_t k_map[16] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U,
    };

    return (dlc < 16U) ? k_map[dlc] : 64U;
}

static uint8_t tsmaster_canfd_len_to_dlc(uint8_t len)
{
    if (len <= 8U) {
        return len;
    }
    if (len <= 12U) {
        return 9U;
    }
    if (len <= 16U) {
        return 10U;
    }
    if (len <= 20U) {
        return 11U;
    }
    if (len <= 24U) {
        return 12U;
    }
    if (len <= 32U) {
        return 13U;
    }
    if (len <= 48U) {
        return 14U;
    }
    return 15U;
}

static uint32_t tsmaster_mask_can_id(uint32_t can_id, bool is_extended)
{
    return can_id & (is_extended ? UDS_TSMASTER_ID_MASK_EXT : UDS_TSMASTER_ID_MASK_STD);
}

static int tsmaster_record_error(uds_transport_t *tp, uds_transport_tsmaster_ctx_t *ctx, int err)
{
    if (ctx != NULL) {
        ctx->last_sdk_error = err;
    }
    if (tp != NULL) {
        tp->last_error = (err != 0) ? err : UDS_ERR_TPORT;
    }
    return -1;
}

static void tsmaster_report_async_error(uds_transport_tsmaster_ctx_t *ctx,
                                        uds_transport_async_error_t err)
{
    if (ctx == NULL || ctx->owner == NULL || ctx->owner->err_cb == NULL) {
        return;
    }

    ctx->owner->err_cb(ctx->owner->err_user, err);
}

static void tsmaster_copy_string(char *dst, size_t dst_size, const char *src, const char *fallback)
{
    const char *chosen = src;

    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (chosen == NULL || chosen[0] == '\0') {
        chosen = fallback;
    }
    if (chosen == NULL) {
        chosen = "";
    }

    (void)snprintf(dst, dst_size, "%s", chosen);
}

static FARPROC tsmaster_get_symbol(HMODULE module, const char *name)
{
    if (module == NULL || name == NULL) {
        return NULL;
    }
    return GetProcAddress(module, name);
}

static HMODULE tsmaster_try_load_library(uds_transport_tsmaster_ctx_t *ctx,
                                         const char *path,
                                         const char *label)
{
    HMODULE module;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    module = LoadLibraryA(path);
    if (module != NULL) {
        fprintf(stderr, "[tsmaster] loaded DLL via %s: %s\n", label, path);
        if (ctx != NULL) {
            ctx->sdk_module_owned = true;
            ctx->last_win32_error = 0;
        }
        return module;
    }

    if (ctx != NULL) {
        ctx->last_win32_error = GetLastError();
    }
    fprintf(stderr, "[tsmaster] LoadLibrary failed via %s: path=%s win32=%lu\n",
            label,
            path,
            (unsigned long)((ctx != NULL) ? ctx->last_win32_error : GetLastError()));
    return NULL;
}

static int tsmaster_load_sdk_symbols(uds_transport_tsmaster_ctx_t *ctx)
{
    const char *env_path;

    if (ctx == NULL) {
        return -1;
    }

    ctx->last_open_stage = "load_dll";
    env_path = getenv("UDS_TSMASTER_DLL_PATH");
    if (env_path != NULL && env_path[0] != '\0') {
        ctx->sdk_module = tsmaster_try_load_library(ctx, env_path, "env:UDS_TSMASTER_DLL_PATH");
    }
    if (ctx->sdk_module == NULL) {
        ctx->sdk_module = tsmaster_try_load_library(ctx, "TSMaster.dll", "default_search");
    }
    if (ctx->sdk_module == NULL && UDS_TSMASTER_DLL_HINT_PATH != NULL) {
        ctx->sdk_module = tsmaster_try_load_library(ctx, UDS_TSMASTER_DLL_HINT_PATH, "cmake_hint_importlib_dir");
    }
    if (ctx->sdk_module == NULL && UDS_TSMASTER_DLL_HINT_PATH_ALT != NULL) {
        ctx->sdk_module = tsmaster_try_load_library(ctx, UDS_TSMASTER_DLL_HINT_PATH_ALT, "cmake_hint_sdk_dir");
    }
    if (ctx->sdk_module == NULL) {
        fprintf(stderr,
                "[tsmaster] unable to load TSMaster.dll; set UDS_TSMASTER_DLL_PATH to the full DLL path if needed\n");
        return -1;
    }

#define UDS_TSMASTER_LOAD_REQUIRED(member, name)                                        \
    do {                                                                                 \
        FARPROC sym__ = tsmaster_get_symbol(ctx->sdk_module, name);                     \
        if (sym__ == NULL) {                                                             \
            ctx->last_open_stage = name;                                                 \
            ctx->last_win32_error = GetLastError();                                      \
            fprintf(stderr,                                                               \
                    "[tsmaster] missing symbol %s win32=%lu\n",                        \
                    name,                                                                \
                    (unsigned long)ctx->last_win32_error);                               \
            return -1;                                                                   \
        }                                                                                \
        memset(&ctx->member, 0, sizeof(ctx->member));                                    \
        memcpy(&ctx->member, &sym__, sizeof(ctx->member));                               \
    } while (0)

    UDS_TSMASTER_LOAD_REQUIRED(initialize_lib_tsmaster,
                               "initialize_lib_tsmaster");
    UDS_TSMASTER_LOAD_REQUIRED(finalize_lib_tsmaster,
                               "finalize_lib_tsmaster");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_set_current_application,
                               "tsapp_set_current_application");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_set_can_channel_count,
                               "tsapp_set_can_channel_count");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_set_mapping,
                               "tsapp_set_mapping");
    do {
        FARPROC sym__ = tsmaster_get_symbol(ctx->sdk_module, "tsapp_get_mapping");
        if (sym__ == NULL) {
            sym__ = tsmaster_get_symbol(ctx->sdk_module, "get_mapping");
        }
        if (sym__ != NULL) {
            memset(&ctx->tsapp_get_mapping, 0, sizeof(ctx->tsapp_get_mapping));
            memcpy(&ctx->tsapp_get_mapping, &sym__, sizeof(ctx->tsapp_get_mapping));
        }
    } while (0);
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_configure_baudrate_can,
                               "tsapp_configure_baudrate_can");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_configure_baudrate_canfd,
                               "tsapp_configure_baudrate_canfd");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_connect,
                               "tsapp_connect");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_disconnect,
                               "tsapp_disconnect");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_register_event_can,
                               "tsapp_register_event_can");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_unregister_event_can,
                               "tsapp_unregister_event_can");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_register_event_canfd,
                               "tsapp_register_event_canfd");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_unregister_event_canfd,
                               "tsapp_unregister_event_canfd");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_transmit_can_async,
                               "tsapp_transmit_can_async");
    UDS_TSMASTER_LOAD_REQUIRED(tsapp_transmit_canfd_async,
                               "tsapp_transmit_canfd_async");

#undef UDS_TSMASTER_LOAD_REQUIRED

    ctx->last_open_stage = "symbols_loaded";
    return 0;
}


static void tsmaster_dump_mapping(const char *tag, const TLIBTSMapping *mapping)
{
    if (mapping == NULL) {
        return;
    }

    fprintf(stderr,
            "[tsmaster] %s app=%s app_ch=%d app_type=%d hw=%s hw_type=%d hw_sub=%d hw_idx=%d hw_ch=%d disabled=%d\n",
            (tag != NULL) ? tag : "mapping",
            mapping->FAppName,
            (int)mapping->FAppChannelIndex,
            (int)mapping->FAppChannelType,
            mapping->FHWDeviceName,
            (int)mapping->FHWDeviceType,
            (int)mapping->FHWDeviceSubType,
            (int)mapping->FHWIndex,
            (int)mapping->FHWChannelIndex,
            (int)mapping->FMappingDisabled);
}

static bool tsmaster_try_get_existing_mapping(uds_transport_tsmaster_ctx_t *ctx,
                                              TLIBTSMapping *mapping_out)
{
    int sdk_rc;

    if (ctx == NULL || mapping_out == NULL || ctx->tsapp_get_mapping == NULL) {
        return false;
    }

    memset(mapping_out, 0, sizeof(*mapping_out));
    tsmaster_copy_string(mapping_out->FAppName,
                         sizeof(mapping_out->FAppName),
                         ctx->app_name,
                         UDS_TSMASTER_DEFAULT_APP_NAME);
    mapping_out->FAppChannelIndex = ctx->app_channel_index;
    mapping_out->FAppChannelType = UDS_TSMASTER_APP_CAN;

    sdk_rc = ctx->tsapp_get_mapping(mapping_out);
    if (sdk_rc != 0) {
        fprintf(stderr,
                "[tsmaster] get_mapping probe failed sdk=%d app=%s app_ch=%d\n",
                sdk_rc,
                ctx->app_name,
                (int)ctx->app_channel_index);
        return false;
    }

    tsmaster_dump_mapping("existing_mapping", mapping_out);

    if (mapping_out->FMappingDisabled != 0) {
        fprintf(stderr, "[tsmaster] existing mapping is disabled; ignoring it\n");
        return false;
    }
    if (mapping_out->FHWDeviceName[0] == '\0') {
        fprintf(stderr, "[tsmaster] existing mapping has empty hardware name; ignoring it\n");
        return false;
    }

    return true;
}

static void tsmaster_queue_reset(uds_transport_tsmaster_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    ctx->rx_count = 0U;
    ctx->rx_overflow = false;
    ctx->unsupported_fd_len_seen = false;
}

static int tsmaster_queue_push(uds_transport_tsmaster_ctx_t *ctx,
                               uint32_t can_id,
                               const uint8_t *data,
                               uint8_t len,
                               bool is_fd)
{
    uds_tsmaster_rx_frame_t *slot;

    if (ctx == NULL || data == NULL) {
        return -1;
    }

    if (ctx->queue_lock_initialized) {
        EnterCriticalSection(&ctx->queue_lock);
    }

    if (ctx->rx_count >= UDS_TSMASTER_RX_QUEUE_LEN) {
        ctx->rx_overflow = true;
        if (ctx->queue_lock_initialized) {
            LeaveCriticalSection(&ctx->queue_lock);
        }
        return -1;
    }

    slot = &ctx->rx_queue[ctx->rx_tail];
    memset(slot, 0, sizeof(*slot));
    slot->can_id = can_id;
    slot->len = len;
    slot->is_fd = is_fd ? 1U : 0U;
    memcpy(slot->data, data, len);

    ctx->rx_tail = (ctx->rx_tail + 1U) % UDS_TSMASTER_RX_QUEUE_LEN;
    ctx->rx_count++;

    if (ctx->queue_lock_initialized) {
        LeaveCriticalSection(&ctx->queue_lock);
    }

    return 0;
}

static int tsmaster_queue_pop(uds_transport_tsmaster_ctx_t *ctx, uds_tsmaster_rx_frame_t *out)
{
    if (ctx == NULL || out == NULL) {
        return -1;
    }

    if (ctx->queue_lock_initialized) {
        EnterCriticalSection(&ctx->queue_lock);
    }

    if (ctx->rx_count == 0U) {
        if (ctx->queue_lock_initialized) {
            LeaveCriticalSection(&ctx->queue_lock);
        }
        return 0;
    }

    *out = ctx->rx_queue[ctx->rx_head];
    memset(&ctx->rx_queue[ctx->rx_head], 0, sizeof(ctx->rx_queue[ctx->rx_head]));
    ctx->rx_head = (ctx->rx_head + 1U) % UDS_TSMASTER_RX_QUEUE_LEN;
    ctx->rx_count--;

    if (ctx->queue_lock_initialized) {
        LeaveCriticalSection(&ctx->queue_lock);
    }

    return 1;
}

static void tsmaster_feed_rx_queue(uds_transport_tsmaster_ctx_t *ctx)
{
    uds_tsmaster_rx_frame_t frame;

    if (ctx == NULL) {
        return;
    }

    while (tsmaster_queue_pop(ctx, &frame) > 0) {
        if (frame.can_id != ctx->phys_rx_can_id) {
            continue;
        }

        if (frame.is_fd && frame.len > 8U) {
            ctx->unsupported_fd_len_seen = true;
            continue;
        }

        isotp_on_can_message(&ctx->isotp.phys_link, frame.data, frame.len);
    }
}

static void tsmaster_on_can_event_impl(uds_tsmaster_callback_obj_t obj, uint32_t can_id, const uint8_t *data, uint8_t len)
{
    uds_transport_tsmaster_ctx_t *ctx = g_tsmaster_active_ctx;

    if (ctx == NULL || obj != ctx->callback_tag || data == NULL || len == 0U || len > 64U) {
        return;
    }

    (void)tsmaster_queue_push(ctx, can_id, data, len, false);
}

static void tsmaster_on_canfd_event_impl(uds_tsmaster_callback_obj_t obj, uint32_t can_id, const uint8_t *data, uint8_t len)
{
    uds_transport_tsmaster_ctx_t *ctx = g_tsmaster_active_ctx;

    if (ctx == NULL || obj != ctx->callback_tag || data == NULL || len == 0U || len > 64U) {
        return;
    }

    (void)tsmaster_queue_push(ctx, can_id, data, len, true);
}

static void __stdcall tsmaster_can_callback(const uds_tsmaster_callback_obj_t obj, const PLIBCAN msg)
{
    uint8_t len;

    if (msg == NULL) {
        return;
    }
    if ((msg->FProperties & UDS_TSMASTER_CAN_PROP_TX) != 0U) {
        return;
    }

    len = (msg->FDLC <= 8U) ? msg->FDLC : 8U;
    if (len == 0U) {
        return;
    }

    tsmaster_on_can_event_impl(obj,
                               tsmaster_mask_can_id((uint32_t)msg->FIdentifier,
                                                    (msg->FProperties & UDS_TSMASTER_CAN_PROP_EXT) != 0U),
                               msg->FData,
                               len);
}

static void __stdcall tsmaster_canfd_callback(const uds_tsmaster_callback_obj_t obj, const PLIBCANFD msg)
{
    uint8_t len;

    if (msg == NULL) {
        return;
    }
    if ((msg->FProperties & UDS_TSMASTER_CAN_PROP_TX) != 0U) {
        return;
    }

    len = tsmaster_canfd_dlc_to_len(msg->FDLC);
    if (len == 0U) {
        return;
    }

    tsmaster_on_canfd_event_impl(obj,
                                 tsmaster_mask_can_id((uint32_t)msg->FIdentifier,
                                                      (msg->FProperties & UDS_TSMASTER_CAN_PROP_EXT) != 0U),
                                 msg->FData,
                                 len);
}

uint32_t isotp_user_get_us(void)
{
    return (uint32_t)(GetTickCount64() * 1000ULL);
}

void isotp_user_debug(const char *message, ...)
{
    (void)message;
}

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size,
                        void *arg)
{
    uds_transport_tsmaster_ctx_t *ctx = (uds_transport_tsmaster_ctx_t *)arg;

    if (ctx == NULL || data == NULL || size == 0U || size > 64U) {
        return ISOTP_RET_ERROR;
    }
    if (!ctx->connected) {
        return ISOTP_RET_ERROR;
    }

    if (ctx->use_canfd) {
        TLIBCANFD frame;
        memset(&frame, 0, sizeof(frame));
        frame.FIdxChn = (uint8_t)ctx->app_channel_index;
        frame.FProperties = UDS_TSMASTER_CAN_PROP_TX |
                            (ctx->use_extended_ids ? UDS_TSMASTER_CAN_PROP_EXT : 0U);
        frame.FFDProperties = UDS_TSMASTER_CANFD_PROP_EDL |
                              (ctx->use_brs ? UDS_TSMASTER_CANFD_PROP_BRS : 0U);
        frame.FDLC = tsmaster_canfd_len_to_dlc(size);
        frame.FIdentifier = (int32_t)tsmaster_mask_can_id(arbitration_id, ctx->use_extended_ids);
        memcpy(frame.FData, data, size);

        if (ctx->tsapp_transmit_canfd_async(&frame) != 0) {
            ctx->last_sdk_error = UDS_ERR_TPORT;
            return ISOTP_RET_ERROR;
        }
        return ISOTP_RET_OK;
    }

    {
        TLIBCAN frame;
        memset(&frame, 0, sizeof(frame));
        frame.FIdxChn = (uint8_t)ctx->app_channel_index;
        frame.FProperties = UDS_TSMASTER_CAN_PROP_TX |
                            (ctx->use_extended_ids ? UDS_TSMASTER_CAN_PROP_EXT : 0U);
        frame.FDLC = (size <= 8U) ? size : 8U;
        frame.FIdentifier = (int32_t)tsmaster_mask_can_id(arbitration_id, ctx->use_extended_ids);
        memcpy(frame.FData, data, frame.FDLC);

        if (ctx->tsapp_transmit_can_async(&frame) != 0) {
            ctx->last_sdk_error = UDS_ERR_TPORT;
            return ISOTP_RET_ERROR;
        }
        return ISOTP_RET_OK;
    }
}

static UDSTpStatus_t tsmaster_intercepted_poll(struct UDSTp *hdl)
{
    uds_transport_tsmaster_ctx_t *ctx = (uds_transport_tsmaster_ctx_t *)hdl;
    UDSTpStatus_t status;

    if (ctx == NULL || ctx->original_poll == NULL) {
        return UDS_TP_ERR;
    }

    tsmaster_feed_rx_queue(ctx);
    status = ctx->original_poll(hdl);

    if (ctx->rx_overflow) {
        ctx->rx_overflow = false;
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        tsmaster_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
    }

    if (ctx->unsupported_fd_len_seen) {
        ctx->unsupported_fd_len_seen = false;
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        tsmaster_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
    }

    return status;
}

static void tsmaster_shutdown_ctx(uds_transport_tsmaster_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->callbacks_registered) {
        if (ctx->tsapp_unregister_event_can != NULL) {
            (void)ctx->tsapp_unregister_event_can(ctx->callback_tag, tsmaster_can_callback);
        }
        if (ctx->tsapp_unregister_event_canfd != NULL) {
            (void)ctx->tsapp_unregister_event_canfd(ctx->callback_tag, tsmaster_canfd_callback);
        }
        ctx->callbacks_registered = false;
    }

    if (ctx->connected) {
        if (ctx->tsapp_disconnect != NULL) {
            (void)ctx->tsapp_disconnect();
        }
        ctx->connected = false;
    }

    if (ctx->original_poll != NULL) {
        ctx->isotp.hdl.poll = ctx->original_poll;
    }
    ctx->original_poll = NULL;

    if (g_tsmaster_active_ctx == ctx) {
        g_tsmaster_active_ctx = NULL;
    }

    if (ctx->sdk_initialized) {
        if (ctx->finalize_lib_tsmaster != NULL) {
            ctx->finalize_lib_tsmaster();
        }
        ctx->sdk_initialized = false;
    }

    if (ctx->sdk_module != NULL) {
        if (ctx->sdk_module_owned) {
            FreeLibrary(ctx->sdk_module);
        }
        ctx->sdk_module = NULL;
        ctx->sdk_module_owned = false;
    }

    if (ctx->queue_lock_initialized) {
        DeleteCriticalSection(&ctx->queue_lock);
        ctx->queue_lock_initialized = false;
    }

    ctx->owner = NULL;
}

static int tsmaster_apply_runtime_cfg(uds_transport_tsmaster_ctx_t *ctx,
                                      const uds_transport_open_cfg_t *cfg)
{
    const uds_transport_tsmaster_cfg_t *backend_cfg;

    if (ctx == NULL || cfg == NULL || cfg->backend_cfg == NULL) {
        return -1;
    }

    backend_cfg = (const uds_transport_tsmaster_cfg_t *)cfg->backend_cfg;

    tsmaster_copy_string(ctx->app_name,
                         sizeof(ctx->app_name),
                         backend_cfg->app_name,
                         UDS_TSMASTER_DEFAULT_APP_NAME);
    tsmaster_copy_string(ctx->hw_name,
                         sizeof(ctx->hw_name),
                         backend_cfg->hw_device_name,
                         UDS_TSMASTER_DEFAULT_HW_NAME);

    ctx->app_channel_index = (int32_t)backend_cfg->app_channel_index;
    ctx->hw_device_type = backend_cfg->hw_device_type;
    ctx->hw_device_sub_type = backend_cfg->hw_device_sub_type;
    ctx->hw_index = backend_cfg->hw_index;
    ctx->hw_channel_index = backend_cfg->hw_channel_index;
    ctx->can_baudrate_kbps = (backend_cfg->can_baudrate_kbps > 0.0f)
                                 ? backend_cfg->can_baudrate_kbps
                                 : (float)UDS_TSMASTER_DEFAULT_CAN_BAUD_KBPS;
    ctx->canfd_arb_baudrate_kbps = (backend_cfg->canfd_arb_baudrate_kbps > 0.0f)
                                       ? backend_cfg->canfd_arb_baudrate_kbps
                                       : (float)UDS_TSMASTER_DEFAULT_CANFD_ARB_KBPS;
    ctx->canfd_data_baudrate_kbps = (backend_cfg->canfd_data_baudrate_kbps > 0.0f)
                                        ? backend_cfg->canfd_data_baudrate_kbps
                                        : (float)UDS_TSMASTER_DEFAULT_CANFD_DATA_KBPS;
    ctx->use_canfd = backend_cfg->use_canfd;
    ctx->use_brs = backend_cfg->use_brs;
    ctx->install_term_resistor = backend_cfg->install_term_resistor;
    ctx->use_extended_ids = backend_cfg->use_extended_ids;

    ctx->phys_rx_can_id = tsmaster_mask_can_id(cfg->phys_sa, ctx->use_extended_ids);
    ctx->phys_tx_can_id = tsmaster_mask_can_id(cfg->phys_ta, ctx->use_extended_ids);
    ctx->func_tx_can_id = tsmaster_mask_can_id(cfg->func_sa, ctx->use_extended_ids);
    ctx->callback_tag = UDS_TSMASTER_CALLBACK_TAG;

    return 0;
}

static int tsmaster_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg)
{
    uds_transport_tsmaster_ctx_t *ctx;
    TLIBTSMapping mapping;
    UDSISOTpCConfig_t isotp_cfg;
    UDSErr_t uds_err;
    int sdk_rc;

    if (tp == NULL || cfg == NULL || cfg->backend_cfg == NULL ||
        cfg->backend != UDS_TRANSPORT_BACKEND_TSMASTER) {
        return -1;
    }

    if (tp->bound_storage == NULL || tp->bound_storage_size < sizeof(*ctx)) {
        tp->last_error = -1;
        return -1;
    }

    if (g_tsmaster_active_ctx != NULL) {
        tp->last_error = UDS_ERR_BUSY;
        return -1;
    }

    ctx = (uds_transport_tsmaster_ctx_t *)tp->bound_storage;
    memset(ctx, 0, sizeof(*ctx));
    ctx->owner = tp;

    if (tsmaster_apply_runtime_cfg(ctx, cfg) != 0) {
        return tsmaster_record_error(tp, ctx, UDS_ERR_INVALID_ARG);
    }

    InitializeCriticalSection(&ctx->queue_lock);
    ctx->queue_lock_initialized = true;
    tsmaster_queue_reset(ctx);

    if (tsmaster_load_sdk_symbols(ctx) != 0) {
        fprintf(stderr, "[tsmaster] open failed at stage=%s sdk=%d win32=%lu\n",
                (ctx->last_open_stage != NULL) ? ctx->last_open_stage : "load_sdk_symbols",
                ctx->last_sdk_error,
                (unsigned long)ctx->last_win32_error);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, UDS_ERR_TPORT);
    }

    ctx->last_open_stage = "initialize_lib_tsmaster";
    sdk_rc = ctx->initialize_lib_tsmaster(ctx->app_name);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] initialize_lib_tsmaster failed sdk=%d\n", sdk_rc);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }
    ctx->sdk_initialized = true;

    ctx->last_open_stage = "tsapp_set_current_application";
    sdk_rc = ctx->tsapp_set_current_application(ctx->app_name);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_set_current_application failed sdk=%d app=%s\n", sdk_rc, ctx->app_name);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }

    ctx->last_open_stage = "tsapp_set_can_channel_count";
    sdk_rc = ctx->tsapp_set_can_channel_count(ctx->app_channel_index + 1);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_set_can_channel_count failed sdk=%d channel_count=%d\n", sdk_rc, (int)(ctx->app_channel_index + 1));
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }

    if (tsmaster_try_get_existing_mapping(ctx, &mapping)) {
        fprintf(stderr, "[tsmaster] using existing GUI/application mapping for connect\n");
        tsmaster_copy_string(ctx->hw_name, sizeof(ctx->hw_name), mapping.FHWDeviceName, ctx->hw_name);
        ctx->hw_device_type = mapping.FHWDeviceType;
        ctx->hw_device_sub_type = mapping.FHWDeviceSubType;
        ctx->hw_index = mapping.FHWIndex;
        ctx->hw_channel_index = mapping.FHWChannelIndex;
    } else {
        memset(&mapping, 0, sizeof(mapping));
        tsmaster_copy_string(mapping.FAppName, sizeof(mapping.FAppName), ctx->app_name, UDS_TSMASTER_DEFAULT_APP_NAME);
        tsmaster_copy_string(mapping.FHWDeviceName, sizeof(mapping.FHWDeviceName), ctx->hw_name, UDS_TSMASTER_DEFAULT_HW_NAME);
        mapping.FAppChannelIndex = ctx->app_channel_index;
        mapping.FAppChannelType = UDS_TSMASTER_APP_CAN;
        mapping.FHWDeviceType = ctx->hw_device_type;
        mapping.FHWDeviceSubType = ctx->hw_device_sub_type;
        mapping.FHWIndex = ctx->hw_index;
        mapping.FHWChannelIndex = ctx->hw_channel_index;
        mapping.FMappingDisabled = 0;
    }

    tsmaster_dump_mapping("requested_mapping", &mapping);

    ctx->last_open_stage = "tsapp_set_mapping";
    sdk_rc = ctx->tsapp_set_mapping(&mapping);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_set_mapping failed sdk=%d app=%s hw=%s app_ch=%d hw_type=%d hw_sub=%d hw_idx=%d hw_ch=%d\n",
                sdk_rc, ctx->app_name, ctx->hw_name, (int)ctx->app_channel_index, (int)ctx->hw_device_type,
                (int)ctx->hw_device_sub_type, (int)ctx->hw_index, (int)ctx->hw_channel_index);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }

    ctx->last_open_stage = ctx->use_canfd ? "tsapp_configure_baudrate_canfd" : "tsapp_configure_baudrate_can";
    if (ctx->use_canfd) {
        sdk_rc = ctx->tsapp_configure_baudrate_canfd(ctx->app_channel_index,
                                                      ctx->canfd_arb_baudrate_kbps,
                                                      ctx->canfd_data_baudrate_kbps,
                                                      UDS_TSMASTER_FD_CONTROLLER_ISO,
                                                      UDS_TSMASTER_FD_MODE_NORMAL,
                                                      ctx->install_term_resistor ? 1 : 0);
    } else {
        sdk_rc = ctx->tsapp_configure_baudrate_can(ctx->app_channel_index,
                                                    ctx->can_baudrate_kbps,
                                                    ctx->install_term_resistor ? 1 : 0);
    }
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] %s failed sdk=%d\n", ctx->last_open_stage != NULL ? ctx->last_open_stage : "baudrate", sdk_rc);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }

    ctx->last_open_stage = "tsapp_connect";
    sdk_rc = ctx->tsapp_connect();
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_connect failed sdk=%d\n", sdk_rc);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }
    ctx->connected = true;

    g_tsmaster_active_ctx = ctx;

    ctx->last_open_stage = "tsapp_register_event_can";
    sdk_rc = ctx->tsapp_register_event_can(ctx->callback_tag, tsmaster_can_callback);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_register_event_can failed sdk=%d\n", sdk_rc);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }
    ctx->last_open_stage = "tsapp_register_event_canfd";
    sdk_rc = ctx->tsapp_register_event_canfd(ctx->callback_tag, tsmaster_canfd_callback);
    if (sdk_rc != 0) {
        fprintf(stderr, "[tsmaster] tsapp_register_event_canfd failed sdk=%d\n", sdk_rc);
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, sdk_rc);
    }
    ctx->callbacks_registered = true;

    memset(&isotp_cfg, 0, sizeof(isotp_cfg));
    isotp_cfg.source_addr = cfg->phys_sa;
    isotp_cfg.target_addr = cfg->phys_ta;
    isotp_cfg.source_addr_func = cfg->phys_sa;
    isotp_cfg.target_addr_func = cfg->func_sa;

    uds_err = UDSISOTpCInit(&ctx->isotp, &isotp_cfg);
    if (uds_err != UDS_OK) {
        tsmaster_shutdown_ctx(ctx);
        return tsmaster_record_error(tp, ctx, (int)uds_err);
    }

    ctx->isotp.phys_link.user_send_can_arg = ctx;
    ctx->isotp.func_link.user_send_can_arg = ctx;
    ctx->original_poll = ctx->isotp.hdl.poll;
    ctx->isotp.hdl.poll = tsmaster_intercepted_poll;

    tp->backend_ctx = ctx;
    tp->last_error = 0;
    return 0;
}

static void tsmaster_close(uds_transport_t *tp)
{
    uds_transport_tsmaster_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return;
    }

    ctx = tsmaster_ctx(tp);
    tsmaster_shutdown_ctx(ctx);

    tp->backend_ctx = NULL;
    tp->last_error = 0;
}

static int tsmaster_send(uds_transport_t *tp,
                         const uint8_t *data,
                         size_t len,
                         bool functional)
{
    uds_transport_tsmaster_ctx_t *ctx;
    UDSSDU_t info;
    ssize_t n;

    if (tp == NULL || data == NULL || len == 0U || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = tsmaster_ctx(tp);
    memset(&info, 0, sizeof(info));
    info.A_TA_Type = functional ? UDS_A_TA_TYPE_FUNCTIONAL : UDS_A_TA_TYPE_PHYSICAL;

    n = UDSTpSend(&ctx->isotp.hdl, data, (ssize_t)len, &info);
    if (n < 0 || (size_t)n != len) {
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }

    return 0;
}

static int tsmaster_poll(uds_transport_t *tp)
{
    uds_transport_tsmaster_ctx_t *ctx;
    UDSTpStatus_t status;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = tsmaster_ctx(tp);
    status = UDSTpPoll(&ctx->isotp.hdl);
    if (status & UDS_TP_ERR) {
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }

    return 0;
}

static void tsmaster_set_timeout(uds_transport_t *tp, uint32_t timeout_ms)
{
    if (tp == NULL) {
        return;
    }

    tp->timeout_ms = timeout_ms;
}

static int tsmaster_get_last_error(uds_transport_t *tp)
{
    if (tp == NULL) {
        return -1;
    }

    return tp->last_error;
}

static UDSTp_t *tsmaster_get_tp_handle(uds_transport_t *tp)
{
    uds_transport_tsmaster_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return NULL;
    }

    ctx = tsmaster_ctx(tp);
    return &ctx->isotp.hdl;
}

static const uds_transport_ops_t g_tsmaster_ops = {
    .open = tsmaster_open,
    .close = tsmaster_close,
    .send = tsmaster_send,
    .poll = tsmaster_poll,
    .set_timeout = tsmaster_set_timeout,
    .get_last_error = tsmaster_get_last_error,
    .get_tp_handle = tsmaster_get_tp_handle,
};

const uds_transport_ops_t *uds_transport_tsmaster_ops(void)
{
    return &g_tsmaster_ops;
}
