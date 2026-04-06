/**
 * @file transport_tsmaster_api.c
 * @brief Windows TSMaster API transport skeleton.
 * @details Task 6B only establishes the build and compile boundary. The actual
 *          initialize/connect/callback/queue/poll pipeline is deferred to Task 7.
 */

#include "transport.h"

#include <string.h>

#include "TSMaster.h"

typedef struct {
    int reserved;
} uds_transport_tsmaster_ctx_t;

_Static_assert(sizeof(uds_transport_tsmaster_ctx_t) <= UDS_TRANSPORT_STORAGE_CAPACITY,
               "UDS_TRANSPORT_STORAGE_CAPACITY is too small for TSMaster backend context");

static int tsmaster_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg)
{
    uds_transport_tsmaster_ctx_t *ctx;

    if (tp == NULL || cfg == NULL || cfg->backend != UDS_TRANSPORT_BACKEND_TSMASTER) {
        return -1;
    }

    if (tp->bound_storage == NULL || tp->bound_storage_size < sizeof(*ctx)) {
        tp->last_error = -1;
        return -1;
    }

    ctx = (uds_transport_tsmaster_ctx_t *)tp->bound_storage;
    memset(ctx, 0, sizeof(*ctx));

    tp->backend_ctx = ctx;
    tp->last_error = UDS_ERR_TPORT;
    return -1;
}

static void tsmaster_close(uds_transport_t *tp)
{
    if (tp == NULL) {
        return;
    }

    tp->backend_ctx = NULL;
    tp->last_error = 0;
}

static int tsmaster_send(uds_transport_t *tp,
                         const uint8_t *data,
                         size_t len,
                         bool functional)
{
    (void)tp;
    (void)data;
    (void)len;
    (void)functional;
    return -1;
}

static int tsmaster_poll(uds_transport_t *tp)
{
    (void)tp;
    return -1;
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
    (void)tp;
    return NULL;
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
