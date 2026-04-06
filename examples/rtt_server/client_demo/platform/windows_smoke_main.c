/**
 * @file windows_smoke_main.c
 * @brief Minimal Windows smoke executable for Task 6B.
 * @details This target verifies that the MSYS2 MINGW64 toolchain can compile the
 *          Windows platform layer and consume the TSMaster SDK include/import-lib
 *          wiring. It is intentionally not the final interactive client.
 */

#include "platform.h"
#include "../transport/transport.h"

#include <stdio.h>

int main(void)
{
    uds_transport_t tp;
    unsigned char storage[UDS_TRANSPORT_STORAGE_CAPACITY];

    uds_transport_init(&tp);
    (void)uds_transport_bind_storage(&tp, storage, sizeof(storage));

    printf("client_demo Windows skeleton configured\\n");
    printf("backend=TSMASTER_API arch=x64 tick_ms=%u\\n", platform_tick_ms());
    return 0;
}
