#pragma once
#if defined(UDS_TP_ISOTP_ZEPHYR)

#include "tp.h"
#include "uds.h"
#include <zephyr/canbus/isotp.h>
#include <zephyr/drivers/can.h>

/**
 * @brief Zephyr native ISO-TP implementation of \ref UDSTp_t
 */
typedef struct {
    /// \cond DOXYGEN_SHOULD_SKIP_THIS
    UDSTp_t hdl;
    const struct device *can_dev;
    struct isotp_recv_ctx phys_rctx;
    struct isotp_recv_ctx func_rctx;
    struct isotp_send_ctx sctx;
    struct isotp_msg_id phys_rx_addr;
    struct isotp_msg_id phys_tx_addr;
    struct isotp_msg_id func_rx_addr;
    /// \endcond
} UDSTpISOTpZephyr_t;

/**
 * @brief Initialize Zephyr native ISO-TP transport for \ref UDSServer_t
 * @param tp \ref UDSTpISOTpZephyr_t instance.
 * @param can_dev CAN device to bind to, e.g. DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))
 * @param source_addr Server listens for physical transmissions on this address.
 * @param target_addr Server sends responses to this address.
 * @param source_addr_func Server listens for functional transmissions on this address.
 */
UDSErr_t UDSServerTpISOTpZephyrInit(UDSTpISOTpZephyr_t *tp, const struct device *can_dev,
                                    uint32_t source_addr, uint32_t target_addr,
                                    uint32_t source_addr_func);

/**
 * @brief Unbind the ISO-TP contexts owned by \p tp
 */
void UDSTpISOTpZephyrDeinit(UDSTpISOTpZephyr_t *tp);

#endif
