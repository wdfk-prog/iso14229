#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>

#include "iso14229.h"

#define PHYS_RX_ADDR 0x7E0
#define PHYS_TX_ADDR 0x7E8
#define FUNC_RX_ADDR 0x7DF

static UDSErr_t fn(UDSServer_t *srv, UDSEvent_t ev, void *arg) {
    (void)srv;
    (void)arg;
    switch (ev) {
    case UDS_EVT_DiagSessCtrl:
    case UDS_EVT_EcuReset:
        return UDS_PositiveResponse;
    case UDS_EVT_DoScheduledReset:
        sys_reboot(SYS_REBOOT_WARM);
        return UDS_OK;
    default:
        return UDS_NRC_ServiceNotSupported;
    }
}

int main(void) {
    static UDSServer_t srv;
    static UDSTpISOTpZephyr_t tp;

    const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    if (UDSServerTpISOTpZephyrInit(&tp, can_dev, PHYS_RX_ADDR, PHYS_TX_ADDR, FUNC_RX_ADDR)) {
        return -1;
    }

    UDSServerInit(&srv);
    srv.tp = &tp.hdl;
    srv.fn = fn;

    while (1) {
        UDSServerPoll(&srv);
        k_msleep(1);
    }
    return 0;
}
