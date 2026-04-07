[中文](api-reference.zh-CN.md) · [Architecture Overview](architecture.md) · [Back to README](../README.md)

# API Reference

This document is a **repository-facing API map**. It focuses on the public headers, runtime entry points, CLI options, and the module boundaries that matter when you integrate or extend the project.

## 1. Client entry points

### 1.1 Process entry

| File | Role |
| --- | --- |
| `client_demo/main.c` | process entry, service registration, connection loop, shell startup |
| `client_demo/core/client_config.c` | command-line parsing and runtime config assembly |
| `client_demo/core/uds_context.c` | owns the UDS client instance, transport lifecycle, and transaction helpers |

### 1.2 Interactive shell entry

| Platform | File | Notes |
| --- | --- | --- |
| Linux / POSIX | `client_demo/core/client_shell.c` | linenoise-based history, completion, redraw |
| Windows | `client_demo/core/client_shell_windows.c` | console-mode aware shell for the Windows build |

## 2. Client runtime configuration API

Header: `client_demo/core/client_config.h`

### 2.1 Runtime configuration types

| Type | Purpose |
| --- | --- |
| `client_transport_backend_t` | selects `socketcan` or `pycan_bridge` |
| `client_socketcan_config_t` | Linux SocketCAN interface name |
| `client_pycan_bridge_config_t` | Python executable, bridge script, interface, channel, bitrate, IPC-related settings |
| `client_runtime_config_t` | top-level configuration object used by the client |

### 2.2 Global runtime config

```c
extern client_runtime_config_t g_uds_cfg;
```

`g_uds_cfg` is the assembled runtime configuration used by the client after command-line parsing.

### 2.3 Public functions

| Function | Purpose |
| --- | --- |
| `const char *client_config_backend_name(client_transport_backend_t backend);` | backend enum to display string |
| `void client_config_parse_args(int argc, char **argv);` | parse CLI options into `g_uds_cfg` |

## 3. Client CLI options

The client exposes the following stable command-line surface from `client_config.c`.

### 3.1 Core options

| Option | Meaning |
| --- | --- |
| `-h`, `--help` | print usage |
| `-b`, `--backend <name>` | `socketcan` or `pycan_bridge` |
| `-s`, `--phys-sa <hex_id>` | physical source ID |
| `-t`, `--phys-ta <hex_id>` | physical target ID |
| `-f`, `--func-sa <hex_id>` | functional address |
| `--timeout-ms <ms>` | transport timeout |

### 3.2 SocketCAN options

| Option | Meaning |
| --- | --- |
| `-i`, `--if-name <name>` | documented example interface name such as `can1`; on Linux it maps to the SocketCAN device |

### 3.3 `pycan_bridge` options

| Option | Meaning |
| --- | --- |
| `--python <exe>` | Python executable used to launch the sidecar |
| `--bridge-script <path>` | path to `client_demo/tools/pycan_bridge.py` |
| `--py-if <name>` | `gs_usb` or `slcan` |
| `--py-channel <name>` | `0` for `gs_usb`, or values like `COM4@9600` for `slcan` |
| `--bitrate <bps>` | arbitration bitrate |
| `--rx-queue <count>` | host-side RX queue depth |
| `--open-timeout-ms <ms>` | sidecar open timeout |
| `--io-timeout-ms <ms>` | bridge command timeout |
| `--canfd` | request CAN FD |
| `--brs` | request bit-rate switching; requires `--canfd` |
| `--extid` | use extended identifiers |
| `--tcp-host <addr>` | debug TCP host |
| `--tcp-port <port>` | debug TCP port |
| `--ipc-tcp` | use debug TCP JSONL instead of stdio JSONL |
| `--no-auto-spawn` | do not spawn the Python sidecar from the C client |

## 4. UDS context API

Header: `client_demo/core/uds_context.h`

### 4.1 Lifecycle

| Function | Purpose |
| --- | --- |
| `UDSClient_t *uds_get_client(void);` | access the singleton client instance |
| `uint8_t uds_get_last_nrc(void);` | read last NRC state |
| `int uds_context_init(void);` | initialize transport and UDS client state |
| `void uds_context_deinit(void);` | close transport and reset context |
| `void uds_register_disconnect_callback(uds_disconnect_callback_t cb);` | register link-loss callback |
| `void uds_register_unsolicited_payload_callback(uds_unsolicited_payload_callback_t cb);` | register unsolicited payload callback |

### 4.2 Transaction helpers

| Function / macro | Purpose |
| --- | --- |
| `void uds_prepare_request(void);` | clear request/response state before a new transaction |
| `int uds_wait_transaction_result(UDSErr_t send_err, const char *msg, uint32_t timeout_ms);` | wait for a request to finish |
| `UDS_TRANSACTION(send_call, msg)` | convenience wrapper using default timeout |
| `UDS_TRANSACTION_TIMEOUT(send_call, msg, ms)` | convenience wrapper with explicit timeout |

## 5. Transport abstraction API

Header: `client_demo/transport/transport.h`

### 5.1 Backend selectors

| Enum / type | Purpose |
| --- | --- |
| `uds_transport_backend_t` | selects `SOCKETCAN`, `TSMASTER`, or `PYCAN_BRIDGE` |
| `uds_transport_open_cfg_t` | common open parameters plus backend-specific config pointer |
| `uds_transport_socketcan_cfg_t` | Linux backend open config |
| `uds_transport_tsmaster_cfg_t` | legacy Windows SDK-bound config |
| `uds_transport_pycan_bridge_cfg_t` | Windows Python-sidecar config |

### 5.2 Public transport functions

| Function | Purpose |
| --- | --- |
| `void uds_transport_init(uds_transport_t *tp);` | reset transport object |
| `int uds_transport_bind_storage(uds_transport_t *tp, void *storage, size_t size);` | bind fixed storage for backend context |
| `int uds_transport_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg);` | open selected backend |
| `void uds_transport_close(uds_transport_t *tp);` | close backend |
| `int uds_transport_send(uds_transport_t *tp, const uint8_t *data, size_t len, bool functional);` | send payload |
| `int uds_transport_poll(uds_transport_t *tp);` | poll backend state |
| `void uds_transport_set_timeout(uds_transport_t *tp, uint32_t timeout_ms);` | update transport timeout |
| `int uds_transport_get_last_error(uds_transport_t *tp);` | query last backend error |
| `UDSTp_t *uds_transport_get_tp_handle(uds_transport_t *tp);` | access embedded ISO-TP handle |
| `void uds_transport_set_error_callback(uds_transport_t *tp, uds_transport_error_callback_t cb, void *user);` | register async transport error callback |

## 6. Backend modules

| File | Role |
| --- | --- |
| `client_demo/transport/transport_socketcan.c` | Linux SocketCAN / ISO-TP backend |
| `client_demo/transport/transport_pycan_bridge.c` | Windows sidecar bridge backend |
| `client_demo/transport/transport_tsmaster_api.c` | legacy TSMaster smoke backend |

## 7. Python-side bridge surface

Primary files:

- `client_demo/tools/pycan_bridge.py`
- `client_demo/tools/pycan_runtime.py`
- `client_demo/tools/requirements-pycan.txt`

### 7.1 Bridge responsibilities

| Layer | Responsibility |
| --- | --- |
| `pycan_bridge.py` | command server and session lifecycle |
| `pycan_runtime.py` | dependency loading, interface validation, bus opening, helper utilities |
| `requirements-pycan.txt` | reproducible Python-side dependencies |

### 7.2 Bridge protocol intent

The repository uses the Python side only for:

- open / close adapter sessions
- send raw CAN frames
- receive raw CAN frames
- emit events back to the C backend

The C side still owns **UDS state**, **ISO-TP state**, and **service command flow**.

## 8. RT-Thread server-side API

Primary headers:

- `server_demo/iso14229_rtt.h`
- `server_demo/rtt_uds_config.h`

### 8.1 Environment lifecycle

| Function | Purpose |
| --- | --- |
| `rtt_uds_env_t *rtt_uds_create(const rtt_uds_config_t *cfg);` | create and initialize one RT-Thread UDS environment |
| `void rtt_uds_destroy(rtt_uds_env_t *env);` | stop and destroy the environment |

### 8.2 Service registration

| Function | Purpose |
| --- | --- |
| `rt_err_t rtt_uds_service_register(rtt_uds_env_t *env, uds_service_node_t *node);` | register one service node |
| `void rtt_uds_service_unregister(uds_service_node_t *node);` | unregister one service node |
| `void rtt_uds_service_unregister_all(rtt_uds_env_t *env);` | clear the dispatch table |

### 8.3 CAN feed path

| Function | Purpose |
| --- | --- |
| `rt_err_t rtt_uds_feed_can_frame(rtt_uds_env_t *env, struct rt_can_msg *msg);` | queue one received CAN frame into the UDS environment |

### 8.4 Helpful macros

| Macro | Purpose |
| --- | --- |
| `RTT_UDS_SERVICE_NODE_INIT(...)` | initialize a runtime service node |
| `RTT_UDS_SERVICE_DEFINE(...)` | define a static service node |
| `RTT_UDS_SERVICE_DECLARE(name)` | declare register / unregister wrappers |
| `RTT_UDS_SERVICE_DEFINE_OPS_PRO(...)` | define register / unregister wrappers with explicit priority and context |
| `RTT_UDS_SERVICE_DEFINE_OPS(...)` | simplified wrapper with default priority and null context |

## 9. Build-facing interface map

| File | Purpose |
| --- | --- |
| `client_demo/CMakeLists.txt` | Linux and Windows build selection |
| `client_demo/CMakePresets.json` | named Windows presets |
| `client_demo/Makefile` | compatibility Linux build path |
| `client_demo/toolchain.cmake` | Yocto / cross Linux toolchain entry |
| `server_demo/Kconfig` | RT-Thread feature toggles |
| `server_demo/SConscript` | RT-Thread source integration |

## 10. Recommended extension points

### Add a new client service command

1. add a new module under `client_demo/services/`
2. register it from `main.c`
3. reuse `uds_context` transaction helpers instead of bypassing transport directly

### Add a new transport backend

1. extend `uds_transport_backend_t`
2. define a backend-specific config struct in `transport.h`
3. add the backend implementation in `client_demo/transport/`
4. keep the backend limited to transport concerns; do not move UDS business logic into it

### Add a new RT-Thread service handler

1. define a `uds_service_node_t`
2. register it with `rtt_uds_service_register()`
3. keep board-specific I/O inside the handler, not in the generic RT-Thread UDS core
