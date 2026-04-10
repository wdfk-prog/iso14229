# RT-Thread server and host client demo {#rtt_server_portal}

The `examples/rtt_server/` tree packages the **RT-Thread target-side UDS server** together with the **Linux / Windows host-side UDS client**. Use this portal when you want the end-to-end bring-up path instead of only the standalone `iso14229.c/.h` integration story.

## Online documentation

- [Client demo online site](https://wdfk-prog.space/iso14229/client_demo/)
- [Root API site main page](mainpage.md)

## Repository documentation map

- [RT-Thread workspace overview](../examples/rtt_server/README.md)
- `server_demo/` submodule documentation set (target-side RT-Thread integration notes)
- [Architecture overview](../examples/rtt_server/docs/architecture.md)
- [API reference](../examples/rtt_server/docs/api-reference.md)
- [Linux build and run guide](../examples/rtt_server/docs/linux-build.md)
- [Windows build guide](../examples/rtt_server/docs/windows-build.md)
- [Python / pip workflow for `PYCAN_BRIDGE`](../examples/rtt_server/docs/pycan-pip-workflow.md)

## Functional overview

### `server_demo/`

`server_demo/` is the RT-Thread-side integration layer. It focuses on:

- binding the generic ISO 14229 core to RT-Thread CAN / ISO-TP paths
- exposing Kconfig and SConscript integration points for package-style enablement
- registering RT-Thread service handlers for diagnostic session, security, communication control, periodic upload, remote console, and file transfer flows
- keeping board / BSP integration details isolated from the protocol core

### `client_demo/`

`client_demo/` is the host-side operator and test client. It provides:

- Linux native SocketCAN ISO-TP execution
- Windows MSYS2-built C client runtime
- Windows `pycan_bridge` transport path for `python-can`-based adapters
- interactive shell, command registry, response dispatch, and local / remote path completion helpers
- request helpers for the main UDS service groups used by the RT-Thread example

## Service groups covered in the demo set

The RT-Thread workspace contains client or server hooks for these service groups:

- `0x10` Diagnostic Session Control
- `0x11` ECU Reset
- `0x22 / 0x2E` Read / Write Data By Identifier
- `0x27` Security Access
- `0x28` Communication Control
- `0x2A` Periodic / ULOG data path
- `0x2F` InputOutputControlByIdentifier
- `0x31` Routine Control / remote console flow
- `0x34 / 0x36 / 0x37 / 0x38` Download and file transfer path
- `0x3E` Tester Present in the runtime workflow

## Recommended entry sequence

1. Start with the [RT-Thread workspace overview](../examples/rtt_server/README.md).
2. Read the `server_demo/` submodule documentation set for target-side integration details.
3. Open the [client demo online site](https://wdfk-prog.space/iso14229/client_demo/) for generated host-side API and source navigation.
4. Use the Linux or Windows build guides based on the host platform you are bringing up.
