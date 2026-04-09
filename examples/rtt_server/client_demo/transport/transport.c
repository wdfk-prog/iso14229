/**
 * @file transport.c
 * @brief Transport abstraction dispatch and lifecycle management.
 */

#include "transport.h"

#include <string.h>

#if defined(UDS_TRANSPORT_ENABLE_SOCKETCAN)
const uds_transport_ops_t *uds_transport_socketcan_ops(void);
#endif

#if defined(UDS_TRANSPORT_ENABLE_TSMASTER)
const uds_transport_ops_t *uds_transport_tsmaster_ops(void);
#endif

#if defined(UDS_TRANSPORT_ENABLE_PYCAN_BRIDGE)
const uds_transport_ops_t *uds_transport_pycan_bridge_ops(void);
#endif

static const uds_transport_ops_t *transport_select_ops(uds_transport_backend_t backend)
{
    switch (backend) {
        case UDS_TRANSPORT_BACKEND_SOCKETCAN:
#if defined(UDS_TRANSPORT_ENABLE_SOCKETCAN)
            return uds_transport_socketcan_ops();
#else
            return NULL;
#endif

        case UDS_TRANSPORT_BACKEND_TSMASTER:
#if defined(UDS_TRANSPORT_ENABLE_TSMASTER)
            return uds_transport_tsmaster_ops();
#else
            return NULL;
#endif

        case UDS_TRANSPORT_BACKEND_PYCAN_BRIDGE:
#if defined(UDS_TRANSPORT_ENABLE_PYCAN_BRIDGE)
            return uds_transport_pycan_bridge_ops();
#else
            return NULL;
#endif

        default:
            return NULL;
    }
}

void uds_transport_init(uds_transport_t *tp)
{
    if (tp == NULL) {
        return;
    }

    memset(tp, 0, sizeof(*tp));
}

int uds_transport_bind_storage(uds_transport_t *tp, void *storage, size_t size)
{
    if (tp == NULL || storage == NULL || size == 0U) {
        return -1;
    }

    if (tp->backend_ctx != NULL || tp->ops != NULL) {
        return -1;
    }

    tp->bound_storage = storage;
    tp->bound_storage_size = size;
    return 0;
}

int uds_transport_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg)
{
    const uds_transport_ops_t *selected_ops;
    int open_ret;

    if (tp == NULL || cfg == NULL) {
        return -1;
    }

    if (tp->backend_ctx != NULL || tp->ops != NULL) {
        return -1;
    }

    if (tp->bound_storage == NULL || tp->bound_storage_size == 0U) {
        return -1;
    }

    selected_ops = transport_select_ops(cfg->backend);
    if (selected_ops == NULL || selected_ops->open == NULL) {
        return -1;
    }

    tp->ops = selected_ops;
    open_ret = tp->ops->open(tp, cfg);
    if (open_ret != 0) {
        /* Defensive rollback for backends that partially initialized context. */
        if (tp->backend_ctx != NULL && tp->ops->close != NULL) {
            tp->ops->close(tp);
        }

        tp->backend_ctx = NULL;
        tp->last_error = 0;
        tp->timeout_ms = 0;
        tp->last_activity_ms = 0;
        tp->err_cb = NULL;
        tp->err_user = NULL;
        tp->ops = NULL;
        return -1;
    }

    return 0;
}

void uds_transport_close(uds_transport_t *tp)
{
    if (tp == NULL) {
        return;
    }

    if (tp->ops != NULL && tp->ops->close != NULL) {
        tp->ops->close(tp);
    }

    tp->backend_ctx = NULL;
    tp->last_error = 0;
    tp->timeout_ms = 0;
    tp->last_activity_ms = 0;
    tp->err_cb = NULL;
    tp->err_user = NULL;
    tp->ops = NULL;
}

int uds_transport_send(uds_transport_t *tp,
                       const uint8_t *data,
                       size_t len,
                       bool functional)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->send == NULL) {
        return -1;
    }

    return tp->ops->send(tp, data, len, functional);
}

int uds_transport_poll(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->poll == NULL) {
        return -1;
    }

    return tp->ops->poll(tp);
}

void uds_transport_set_timeout(uds_transport_t *tp, uint32_t timeout_ms)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->set_timeout == NULL) {
        return;
    }

    tp->ops->set_timeout(tp, timeout_ms);
}

int uds_transport_get_last_error(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->get_last_error == NULL) {
        return -1;
    }

    return tp->ops->get_last_error(tp);
}

uint32_t uds_transport_get_last_activity_ms(uds_transport_t *tp)
{
    if (tp == NULL) {
        return 0U;
    }

    return tp->last_activity_ms;
}

UDSTp_t *uds_transport_get_tp_handle(uds_transport_t *tp)
{
    if (tp == NULL || tp->ops == NULL || tp->ops->get_tp_handle == NULL) {
        return NULL;
    }

    return tp->ops->get_tp_handle(tp);
}

void uds_transport_set_error_callback(uds_transport_t *tp,
                                      uds_transport_error_callback_t cb,
                                      void *user)
{
    if (tp == NULL) {
        return;
    }

    tp->err_cb = cb;
    tp->err_user = user;
}
