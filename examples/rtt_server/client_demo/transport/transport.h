/**
 * @file transport.h
 * @brief Minimal transport abstraction for multi-backend UDS client support.
 *
 * This header defines a compact transport boundary so `uds_context` can switch
 * between Linux SocketCAN and future Windows TSMaster backends while keeping
 * upper-layer UDS command/service code stable.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso14229.h"

/*
 * Static storage budget for backend private contexts.
 *
 * The Windows TSMaster backend embeds an ISO-TP client instance, a receive
 * queue, dynamic-loader state, and synchronization primitives. Keep this value
 * comfortably above the largest backend context so callers can bind a single
 * fixed-size storage block without backend-specific allocation.
 */
#define UDS_TRANSPORT_STORAGE_CAPACITY (64U * 1024U)

/**
 * @brief Backend selector for transport implementations.
 */
typedef enum {
    UDS_TRANSPORT_BACKEND_SOCKETCAN = 0,
    UDS_TRANSPORT_BACKEND_TSMASTER = 1,
} uds_transport_backend_t;

/**
 * @brief Transport asynchronous error category (backend-layer semantics).
 */
typedef enum {
    UDS_TRANSPORT_ASYNC_ERR_POLL = 1,
    UDS_TRANSPORT_ASYNC_ERR_IO,
    UDS_TRANSPORT_ASYNC_ERR_DISCONNECTED,
} uds_transport_async_error_t;

/**
 * @brief Common open arguments for all transport backends.
 * @details Backend-private parameters are passed via @p backend_cfg.
 *          `backend_cfg` is valid only during `uds_transport_open()` call;
 *          backend implementation must copy required fields and must not store
 *          the caller-provided pointer directly.
 */
typedef struct {
    uds_transport_backend_t backend;
    uint32_t phys_sa;
    uint32_t phys_ta;
    uint32_t func_sa;
    const void *backend_cfg;
} uds_transport_open_cfg_t;

/**
 * @brief SocketCAN backend-specific open configuration.
 */
typedef struct {
    const char *if_name;
} uds_transport_socketcan_cfg_t;

/**
 * @brief Windows TSMaster backend-specific open configuration.
 * @details String pointers are borrowed only for the duration of
 *          `uds_transport_open()`; backend implementations must copy them.
 */
typedef struct {
    const char *app_name;
    const char *hw_device_name;
    uint8_t app_channel_index;
    int32_t hw_device_type;
    int32_t hw_device_sub_type;
    int32_t hw_index;
    int32_t hw_channel_index;
    float can_baudrate_kbps;
    float canfd_arb_baudrate_kbps;
    float canfd_data_baudrate_kbps;
    bool use_canfd;
    bool use_brs;
    bool install_term_resistor;
    bool use_extended_ids;
} uds_transport_tsmaster_cfg_t;

/**
 * @brief Transport-layer asynchronous error callback.
 * @param user User data pointer registered by caller.
 * @param err  Transport-layer normalized async error category.
 * @note Backends may report persistent async faults repeatedly; upper layers
 *       should define counting and reset policy explicitly.
 */
typedef void (*uds_transport_error_callback_t)(void *user, uds_transport_async_error_t err);

/** Transport object handle used by upper layers. */
typedef struct uds_transport uds_transport_t;

/**
 * @brief Backend operation table.
 */
typedef struct {
    int (*open)(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg);
    void (*close)(uds_transport_t *tp);
    int (*send)(uds_transport_t *tp, const uint8_t *data, size_t len, bool functional);
    int (*poll)(uds_transport_t *tp);
    void (*set_timeout)(uds_transport_t *tp, uint32_t timeout_ms);
    int (*get_last_error)(uds_transport_t *tp);
    UDSTp_t *(*get_tp_handle)(uds_transport_t *tp);
} uds_transport_ops_t;

/**
 * @brief Public transport instance used by upper layers.
 * @details Backend implementation owns and interprets @p backend_ctx.
 */
struct uds_transport {
    const uds_transport_ops_t *ops;
    void *backend_ctx;
    int last_error;
    uint32_t timeout_ms;
    uds_transport_error_callback_t err_cb;
    void *err_user;

    /* Caller-provided raw storage used by backend context allocation. */
    void *bound_storage;
    size_t bound_storage_size;
};

/**
 * @brief Initialize transport object to a clean closed state.
 */
void uds_transport_init(uds_transport_t *tp);

/**
 * @brief Bind caller-provided raw storage used by backend context.
 * @return 0 on success, -1 on failure.
 */
int uds_transport_bind_storage(uds_transport_t *tp, void *storage, size_t size);

/**
 * @brief Open transport with backend selected by @p cfg.
 * @return 0 on success, -1 on failure.
 * @note Caller must initialize @p tp via uds_transport_init() and bind storage
 *       via uds_transport_bind_storage() before first open.
 * @note On failure, this function rolls @p tp back to a clean closed state.
 */
int uds_transport_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg);

/**
 * @brief Close transport and reset runtime state.
 */
void uds_transport_close(uds_transport_t *tp);

int uds_transport_send(uds_transport_t *tp,
                       const uint8_t *data,
                       size_t len,
                       bool functional);

int uds_transport_poll(uds_transport_t *tp);

void uds_transport_set_timeout(uds_transport_t *tp, uint32_t timeout_ms);

int uds_transport_get_last_error(uds_transport_t *tp);

UDSTp_t *uds_transport_get_tp_handle(uds_transport_t *tp);

void uds_transport_set_error_callback(uds_transport_t *tp,
                                      uds_transport_error_callback_t cb,
                                      void *user);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */
