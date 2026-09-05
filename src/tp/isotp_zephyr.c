#if defined(UDS_TP_ISOTP_ZEPHYR)

#include "util.h"
#include "log.h"
#include "tp/isotp_zephyr.h"

static UDSTpSize_t isotp_zephyr_tp_send(UDSTp_t *hdl, const uint8_t *buf, size_t len,
                                        const UDSSDU_t *info) {
    UDS_ASSERT(hdl);
    UDSTpISOTpZephyr_t *tp = (UDSTpISOTpZephyr_t *)hdl;
    (void)info; // requests/responses are always sent physically
    int ret = isotp_send(&tp->sctx, tp->can_dev, buf, len, &tp->phys_tx_addr, &tp->phys_rx_addr,
                         NULL, NULL);
    return ISOTP_N_OK == ret ? (UDSTpSize_t)len : -1;
}

static UDSTpSize_t isotp_zephyr_tp_recv(UDSTp_t *hdl, uint8_t *buf, size_t bufsize,
                                        UDSSDU_t *info) {
    UDS_ASSERT(hdl);
    UDS_ASSERT(buf);
    UDSTpISOTpZephyr_t *tp = (UDSTpISOTpZephyr_t *)hdl;

    int ret = isotp_recv(&tp->phys_rctx, buf, bufsize, K_NO_WAIT);
    if (ret >= 0) {
        if (info) {
            info->A_TA_Type = UDS_A_TA_TYPE_PHYSICAL;
        }
        return ret;
    }

    ret = isotp_recv(&tp->func_rctx, buf, bufsize, K_NO_WAIT);
    if (ret >= 0) {
        if (info) {
            info->A_TA_Type = UDS_A_TA_TYPE_FUNCTIONAL;
        }
        return ret;
    }

    return 0; // ISOTP_RECV_TIMEOUT (K_NO_WAIT, nothing pending) is not an error
}

static UDSTpStatus_t isotp_zephyr_tp_poll(UDSTp_t *hdl) {
    (void)hdl;
    return UDS_TP_IDLE;
}

UDSErr_t UDSServerTpISOTpZephyrInit(UDSTpISOTpZephyr_t *tp, const struct device *can_dev,
                                    uint32_t source_addr, uint32_t target_addr,
                                    uint32_t source_addr_func) {
    UDS_ASSERT(tp);
    UDS_ASSERT(can_dev);
    if (!device_is_ready(can_dev)) {
        UDS_LOGE(__FILE__, "CAN device is not ready");
        return UDS_FAIL;
    }
    if (can_start(can_dev)) {
        UDS_LOGE(__FILE__, "failed to start CAN device");
        return UDS_FAIL;
    }

    tp->hdl.send = isotp_zephyr_tp_send;
    tp->hdl.recv = isotp_zephyr_tp_recv;
    tp->hdl.poll = isotp_zephyr_tp_poll;
    tp->can_dev = can_dev;
    tp->phys_rx_addr = (struct isotp_msg_id){.std_id = source_addr};
    tp->phys_tx_addr = (struct isotp_msg_id){.std_id = target_addr};
    tp->func_rx_addr = (struct isotp_msg_id){.std_id = source_addr_func};

    const struct isotp_fc_opts fc_opts = {.bs = 8, .stmin = 0};

    if (isotp_bind(&tp->phys_rctx, can_dev, &tp->phys_rx_addr, &tp->phys_tx_addr, &fc_opts,
                   K_FOREVER)) {
        UDS_LOGE(__FILE__, "failed to bind physical ISO-TP address 0x%03x", source_addr);
        return UDS_FAIL;
    }
    if (isotp_bind(&tp->func_rctx, can_dev, &tp->func_rx_addr, &tp->phys_tx_addr, &fc_opts,
                   K_FOREVER)) {
        UDS_LOGE(__FILE__, "failed to bind functional ISO-TP address 0x%03x", source_addr_func);
        isotp_unbind(&tp->phys_rctx);
        return UDS_FAIL;
    }
    return UDS_OK;
}

void UDSTpISOTpZephyrDeinit(UDSTpISOTpZephyr_t *tp) {
    if (tp) {
        isotp_unbind(&tp->phys_rctx);
        isotp_unbind(&tp->func_rctx);
    }
}

#endif
