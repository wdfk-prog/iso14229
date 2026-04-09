/**
 * @file transport_socketcan.c
 * @brief Linux SocketCAN backend for transport abstraction.
 */

#include "transport.h"

#include <stddef.h>
#include <string.h>
#include <unistd.h>

/*
 * Keep UDSTpIsoTpSock_t as the first field of the backend context so the poll
 * callback can safely cast UDSTp handle back to this context (offset zero).
 */
typedef struct {
    UDSTpIsoTpSock_t sock;
    UDSTpStatus_t (*original_poll)(struct UDSTp *hdl);
    uds_transport_t *owner;
} uds_transport_socketcan_ctx_t;

_Static_assert(offsetof(uds_transport_socketcan_ctx_t, sock) == 0,
               "uds_transport_socketcan_ctx_t.sock must be at offset 0");
_Static_assert(offsetof(UDSTpIsoTpSock_t, hdl) == 0,
               "UDSTpIsoTpSock_t.hdl must be at offset 0");
_Static_assert(sizeof(uds_transport_socketcan_ctx_t) <= UDS_TRANSPORT_STORAGE_CAPACITY,
               "UDS_TRANSPORT_STORAGE_CAPACITY is too small for SocketCAN backend context");

static uds_transport_socketcan_ctx_t *socketcan_ctx(uds_transport_t *tp)
{
    return (uds_transport_socketcan_ctx_t *)tp->backend_ctx;
}

static void socketcan_shutdown_ctx(uds_transport_socketcan_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->original_poll != NULL) {
        ctx->sock.hdl.poll = ctx->original_poll;
    }

    /* FD teardown ownership: UDSTpIsoTpSockDeinit closes phys_fd/func_fd in current iso14229 implementation (src/tp/isotp_sock.c). */
    if (ctx->sock.phys_fd >= 0 || ctx->sock.func_fd >= 0) {
        UDSTpIsoTpSockDeinit(&ctx->sock);
    }

    ctx->sock.phys_fd = -1;
    ctx->sock.func_fd = -1;
    ctx->original_poll = NULL;
    ctx->owner = NULL;
}

static UDSTpStatus_t socketcan_intercepted_poll(struct UDSTp *hdl)
{
    uds_transport_socketcan_ctx_t *ctx = (uds_transport_socketcan_ctx_t *)hdl;
    UDSTpStatus_t status;

    if (ctx == NULL || ctx->original_poll == NULL) {
        return UDS_TP_ERR;
    }

    status = ctx->original_poll(hdl);
    if (ctx->owner != NULL && status != UDS_TP_IDLE) {
        ctx->owner->last_activity_ms = UDSMillis();
    }
    if (status & UDS_TP_ERR) {
        if (ctx->owner) {
            ctx->owner->last_error = UDS_ERR_TPORT;
            if (ctx->owner->err_cb) {
                ctx->owner->err_cb(ctx->owner->err_user, UDS_TRANSPORT_ASYNC_ERR_POLL);
            }
        }
    }

    return status;
}

static int socketcan_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg)
{
    const uds_transport_socketcan_cfg_t *backend_cfg;
    uds_transport_socketcan_ctx_t *ctx;
    UDSErr_t err;

    if (tp == NULL || cfg == NULL || cfg->backend_cfg == NULL ||
        cfg->backend != UDS_TRANSPORT_BACKEND_SOCKETCAN) {
        return -1;
    }

    if (tp->bound_storage == NULL || tp->bound_storage_size < sizeof(*ctx)) {
        tp->last_error = -1;
        return -1;
    }

    backend_cfg = (const uds_transport_socketcan_cfg_t *)cfg->backend_cfg;
    if (backend_cfg->if_name == NULL) {
        return -1;
    }

    ctx = (uds_transport_socketcan_ctx_t *)tp->bound_storage;
    memset(ctx, 0, sizeof(*ctx));

    ctx->owner = tp;
    ctx->sock.phys_fd = -1;
    ctx->sock.func_fd = -1;

    err = UDSTpIsoTpSockInitClient(&ctx->sock,
                                   backend_cfg->if_name,
                                   cfg->phys_sa,
                                   cfg->phys_ta,
                                   cfg->func_sa);
    if (err != UDS_OK) {
        /* Defensive cleanup in case init opened file descriptors before failing. */
        socketcan_shutdown_ctx(ctx);
        tp->last_error = (int)err;
        return -1;
    }

    if (ctx->sock.hdl.poll == NULL) {
        socketcan_shutdown_ctx(ctx);
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }

    ctx->original_poll = ctx->sock.hdl.poll;
    ctx->sock.hdl.poll = socketcan_intercepted_poll;

    tp->backend_ctx = ctx;
    tp->last_error = 0;
    return 0;
}

static void socketcan_close(uds_transport_t *tp)
{
    uds_transport_socketcan_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return;
    }

    ctx = socketcan_ctx(tp);
    socketcan_shutdown_ctx(ctx);

    tp->backend_ctx = NULL;
    tp->last_error = 0;
}

static int socketcan_send(uds_transport_t *tp,
                          const uint8_t *data,
                          size_t len,
                          bool functional)
{
    uds_transport_socketcan_ctx_t *ctx;
    UDSSDU_t info;
    ssize_t n;

    if (tp == NULL || data == NULL || len == 0U || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = socketcan_ctx(tp);
    memset(&info, 0, sizeof(info));
    info.A_TA_Type = functional ? UDS_A_TA_TYPE_FUNCTIONAL : UDS_A_TA_TYPE_PHYSICAL;

    n = UDSTpSend(&ctx->sock.hdl, data, (ssize_t)len, &info);
    if (n < 0 || (size_t)n != len) {
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }

    tp->last_activity_ms = UDSMillis();
    return 0;
}

static int socketcan_poll(uds_transport_t *tp)
{
    uds_transport_socketcan_ctx_t *ctx;
    UDSTpStatus_t status;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = socketcan_ctx(tp);
    status = UDSTpPoll(&ctx->sock.hdl);
    if (status & UDS_TP_ERR) {
        tp->last_error = UDS_ERR_TPORT;
        tp->last_activity_ms = UDSMillis();
        return -1;
    }

    if (status != UDS_TP_IDLE) {
        tp->last_activity_ms = UDSMillis();
    }

    return 0;
}

static void socketcan_set_timeout(uds_transport_t *tp, uint32_t timeout_ms)
{
    if (tp == NULL) {
        return;
    }

    tp->timeout_ms = timeout_ms;
}

static int socketcan_get_last_error(uds_transport_t *tp)
{
    if (tp == NULL) {
        return -1;
    }

    return tp->last_error;
}

static UDSTp_t *socketcan_get_tp_handle(uds_transport_t *tp)
{
    uds_transport_socketcan_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return NULL;
    }

    ctx = socketcan_ctx(tp);
    return &ctx->sock.hdl;
}

static const uds_transport_ops_t g_socketcan_ops = {
    .open = socketcan_open,
    .close = socketcan_close,
    .send = socketcan_send,
    .poll = socketcan_poll,
    .set_timeout = socketcan_set_timeout,
    .get_last_error = socketcan_get_last_error,
    .get_tp_handle = socketcan_get_tp_handle,
};

const uds_transport_ops_t *uds_transport_socketcan_ops(void)
{
    return &g_socketcan_ops;
}
