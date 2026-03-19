/**
 * @file transport.h
 * @brief Minimal transport abstraction for multi-backend UDS client support.
 *
 * This header defines a small, C-friendly abstraction boundary so `uds_context`
 * can later switch between Linux SocketCAN and Windows TSMaster backends without
 * changing upper-layer command/service logic.
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

/**
 * @brief Backend selector for transport implementations.
 */
typedef enum {
    UDS_TRANSPORT_BACKEND_SOCKETCAN = 0,
    UDS_TRANSPORT_BACKEND_TSMASTER = 1,
} uds_transport_backend_t;

/**
 * @brief Common transport open arguments used by upper layers.
 * @details `backend_cfg` is backend-private opaque data, so this struct does
 *          not leak SocketCAN/TSMaster-specific fields to all callers.
 */
typedef struct {
    uds_transport_backend_t backend;
    uint32_t phys_sa;
    uint32_t phys_ta;
    uint32_t func_sa;
    const void *backend_cfg;
} uds_transport_open_cfg_t;

/** Opaque transport object. */
typedef struct uds_transport uds_transport_t;

/**
 * @brief Backend operation table.
 * @details Keep this set minimal: lifecycle, send/poll, timeout and error.
 */
typedef struct {
    int (*open)(uds_transport_t *tp, const void *cfg);
    void (*close)(uds_transport_t *tp);
    int (*send)(uds_transport_t *tp, const uint8_t *data, size_t len, bool functional);
    int (*poll)(uds_transport_t *tp);
    void (*set_timeout)(uds_transport_t *tp, uint32_t timeout_ms);
    int (*get_last_error)(uds_transport_t *tp);
    UDSTp_t *(*get_tp_handle)(uds_transport_t *tp);
} uds_transport_ops_t;

/**
 * @brief Transport instance container.
 * @details `backend_ctx` is owned by backend implementation.
 */
struct uds_transport {
    const uds_transport_ops_t *ops;
    void *backend_ctx;
    int last_error;
    uint32_t timeout_ms;
};

/* ---------- Thin dispatch helpers (no behavior change by themselves) ---------- */

static inline int uds_transport_open(uds_transport_t *tp, const void *cfg)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->open == NULL) {
        return -1;
    }
    return tp->ops->open(tp, cfg);
}

static inline void uds_transport_close(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->close == NULL) {
        return;
    }
    tp->ops->close(tp);
}

static inline int uds_transport_send(uds_transport_t *tp,
                                     const uint8_t *data,
                                     size_t len,
                                     bool functional)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->send == NULL) {
        return -1;
    }
    return tp->ops->send(tp, data, len, functional);
}

static inline int uds_transport_poll(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->poll == NULL) {
        return -1;
    }
    return tp->ops->poll(tp);
}

static inline void uds_transport_set_timeout(uds_transport_t *tp, uint32_t timeout_ms)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->set_timeout == NULL) {
        return;
    }
    tp->ops->set_timeout(tp, timeout_ms);
}

static inline int uds_transport_get_last_error(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->get_last_error == NULL) {
        return -1;
    }
    return tp->ops->get_last_error(tp);
}

static inline UDSTp_t *uds_transport_get_tp_handle(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->get_tp_handle == NULL) {
        return NULL;
    }
    return tp->ops->get_tp_handle(tp);
}

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */
