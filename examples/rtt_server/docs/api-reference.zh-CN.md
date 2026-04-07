[English](api-reference.md) · [整体架构总览](architecture.zh-CN.md) · [返回 README](../README_ZN.md)

# API 参考

本文档是一个面向仓库的 **API 地图**。它聚焦于项目里真正需要关注的公共头文件、运行入口、CLI 参数与模块边界，便于你做集成或扩展。

## 1. 客户端入口

### 1.1 进程入口

| 文件 | 作用 |
| --- | --- |
| `client_demo/main.c` | 进程入口、服务注册、连接循环、shell 启动 |
| `client_demo/core/client_config.c` | 命令行解析与运行配置组装 |
| `client_demo/core/uds_context.c` | 持有 UDS client 实例、transport 生命周期与事务辅助逻辑 |

### 1.2 交互 shell 入口

| 平台 | 文件 | 说明 |
| --- | --- | --- |
| Linux / POSIX | `client_demo/core/client_shell.c` | 基于 linenoise，支持历史、补全与重绘 |
| Windows | `client_demo/core/client_shell_windows.c` | 面向 Windows console 的独立 shell 实现 |

## 2. 客户端运行配置 API

头文件：`client_demo/core/client_config.h`

### 2.1 运行配置类型

| 类型 | 作用 |
| --- | --- |
| `client_transport_backend_t` | 选择 `socketcan` 或 `pycan_bridge` |
| `client_socketcan_config_t` | Linux SocketCAN 接口名 |
| `client_pycan_bridge_config_t` | Python 解释器、bridge 脚本、接口名、channel、bitrate、IPC 相关设置 |
| `client_runtime_config_t` | 客户端顶层运行配置对象 |

### 2.2 全局运行配置

```c
extern client_runtime_config_t g_uds_cfg;
```

`g_uds_cfg` 是命令行解析后的运行配置汇总对象。

### 2.3 公共函数

| 函数 | 作用 |
| --- | --- |
| `const char *client_config_backend_name(client_transport_backend_t backend);` | 将后端枚举转为显示字符串 |
| `void client_config_parse_args(int argc, char **argv);` | 将 CLI 选项解析进 `g_uds_cfg` |

## 3. 客户端 CLI 参数

客户端在 `client_config.c` 中暴露了以下稳定命令行接口。

除非后面另外追加后端专用参数，文档中的示例默认以这条基准命令展开：

```bash
./client -i can1 -s 7D1 -t 7E1 -f 7E0
```

### 3.1 核心参数

| 参数 | 含义 |
| --- | --- |
| `-h`, `--help` | 打印帮助 |
| `-b`, `--backend <name>` | `socketcan` 或 `pycan_bridge` |
| `-s`, `--phys-sa <hex_id>` | 物理源地址 |
| `-t`, `--phys-ta <hex_id>` | 物理目标地址 |
| `-f`, `--func-sa <hex_id>` | 功能地址 |
| `--timeout-ms <ms>` | transport 超时 |

### 3.2 SocketCAN 参数

| 参数 | 含义 |
| --- | --- |
| `-i`, `--if-name <name>` | 文档中的统一示例接口名，例如 `can1`；在 Linux 下它映射到具体 SocketCAN 设备 |

### 3.3 `pycan_bridge` 参数

| 参数 | 含义 |
| --- | --- |
| `--python <exe>` | 用于拉起 sidecar 的 Python 解释器 |
| `--bridge-script <path>` | `client_demo/tools/pycan_bridge.py` 路径 |
| `--py-if <name>` | `gs_usb` 或 `slcan` |
| `--py-channel <name>` | `gs_usb` 用 `0`，`slcan` 用 `COM4@9600` 这类字符串 |
| `--bitrate <bps>` | 仲裁速率 |
| `--rx-queue <count>` | 主机侧 RX 队列深度 |
| `--open-timeout-ms <ms>` | sidecar 打开超时 |
| `--io-timeout-ms <ms>` | bridge 命令超时 |
| `--canfd` | 请求使用 CAN FD |
| `--brs` | 请求 BRS，必须配合 `--canfd` |
| `--extid` | 使用扩展帧 ID |
| `--tcp-host <addr>` | 调试 TCP host |
| `--tcp-port <port>` | 调试 TCP 端口 |
| `--ipc-tcp` | 调试时改用 TCP JSONL，而不是 stdio JSONL |
| `--no-auto-spawn` | 不由 C 客户端自动拉起 Python sidecar |

## 4. UDS context API

头文件：`client_demo/core/uds_context.h`

### 4.1 生命周期

| 函数 | 作用 |
| --- | --- |
| `UDSClient_t *uds_get_client(void);` | 获取单例 client 实例 |
| `uint8_t uds_get_last_nrc(void);` | 读取最近一次 NRC |
| `int uds_context_init(void);` | 初始化 transport 与 UDS client 状态 |
| `void uds_context_deinit(void);` | 关闭 transport 并重置上下文 |
| `void uds_register_disconnect_callback(uds_disconnect_callback_t cb);` | 注册断链回调 |
| `void uds_register_unsolicited_payload_callback(uds_unsolicited_payload_callback_t cb);` | 注册非请求型 payload 回调 |

### 4.2 事务辅助接口

| 函数 / 宏 | 作用 |
| --- | --- |
| `void uds_prepare_request(void);` | 发起新事务前清理请求状态 |
| `int uds_wait_transaction_result(UDSErr_t send_err, const char *msg, uint32_t timeout_ms);` | 等待一次请求完成 |
| `UDS_TRANSACTION(send_call, msg)` | 使用默认超时的便捷事务封装 |
| `UDS_TRANSACTION_TIMEOUT(send_call, msg, ms)` | 自定义超时的事务封装 |

## 5. Transport 抽象 API

头文件：`client_demo/transport/transport.h`

### 5.1 后端选择与配置

| 枚举 / 类型 | 作用 |
| --- | --- |
| `uds_transport_backend_t` | 选择 `SOCKETCAN`、`TSMASTER` 或 `PYCAN_BRIDGE` |
| `uds_transport_open_cfg_t` | 公共 open 参数 + 后端私有配置指针 |
| `uds_transport_socketcan_cfg_t` | Linux 后端配置 |
| `uds_transport_tsmaster_cfg_t` | 历史 Windows SDK 路径配置 |
| `uds_transport_pycan_bridge_cfg_t` | Windows Python sidecar 配置 |

### 5.2 公共 transport 函数

| 函数 | 作用 |
| --- | --- |
| `void uds_transport_init(uds_transport_t *tp);` | 重置 transport 对象 |
| `int uds_transport_bind_storage(uds_transport_t *tp, void *storage, size_t size);` | 绑定固定大小的后端上下文存储 |
| `int uds_transport_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg);` | 打开选定后端 |
| `void uds_transport_close(uds_transport_t *tp);` | 关闭后端 |
| `int uds_transport_send(uds_transport_t *tp, const uint8_t *data, size_t len, bool functional);` | 发送 payload |
| `int uds_transport_poll(uds_transport_t *tp);` | 轮询后端 |
| `void uds_transport_set_timeout(uds_transport_t *tp, uint32_t timeout_ms);` | 更新 transport 超时 |
| `int uds_transport_get_last_error(uds_transport_t *tp);` | 查询最近一次后端错误 |
| `UDSTp_t *uds_transport_get_tp_handle(uds_transport_t *tp);` | 获取内嵌 ISO-TP 句柄 |
| `void uds_transport_set_error_callback(uds_transport_t *tp, uds_transport_error_callback_t cb, void *user);` | 注册异步 transport 错误回调 |

## 6. 后端模块

| 文件 | 作用 |
| --- | --- |
| `client_demo/transport/transport_socketcan.c` | Linux SocketCAN / ISO-TP 后端 |
| `client_demo/transport/transport_pycan_bridge.c` | Windows Python sidecar bridge 后端 |
| `client_demo/transport/transport_tsmaster_api.c` | 历史 TSMaster smoke 后端 |

## 7. Python side bridge 接口面

核心文件：

- `client_demo/tools/pycan_bridge.py`
- `client_demo/tools/pycan_runtime.py`
- `client_demo/tools/requirements-pycan.txt`

### 7.1 Bridge 职责划分

| 层 | 职责 |
| --- | --- |
| `pycan_bridge.py` | 命令服务器与会话生命周期 |
| `pycan_runtime.py` | 依赖加载、接口校验、bus 打开与公共辅助函数 |
| `requirements-pycan.txt` | Python 侧可复现依赖清单 |

### 7.2 Bridge 协议定位

当前仓库中，Python 侧只负责：

- 打开 / 关闭适配器会话
- 发送原始 CAN 帧
- 接收原始 CAN 帧
- 向 C 后端回传事件

C 侧仍然持有 **UDS 状态**、**ISO-TP 状态** 与 **服务命令流程**。

## 8. RT-Thread 服务端 API

核心头文件：

- `server_demo/iso14229_rtt.h`
- `server_demo/rtt_uds_config.h`

### 8.1 环境生命周期

| 函数 | 作用 |
| --- | --- |
| `rtt_uds_env_t *rtt_uds_create(const rtt_uds_config_t *cfg);` | 创建并初始化一个 RT-Thread UDS 环境 |
| `void rtt_uds_destroy(rtt_uds_env_t *env);` | 停止并销毁环境 |

### 8.2 服务注册

| 函数 | 作用 |
| --- | --- |
| `rt_err_t rtt_uds_service_register(rtt_uds_env_t *env, uds_service_node_t *node);` | 注册一个服务节点 |
| `void rtt_uds_service_unregister(uds_service_node_t *node);` | 注销一个服务节点 |
| `void rtt_uds_service_unregister_all(rtt_uds_env_t *env);` | 清空整个分发表 |

### 8.3 CAN 输入路径

| 函数 | 作用 |
| --- | --- |
| `rt_err_t rtt_uds_feed_can_frame(rtt_uds_env_t *env, struct rt_can_msg *msg);` | 将一帧接收到的 CAN 数据送入 UDS 环境 |

### 8.4 常用宏

| 宏 | 作用 |
| --- | --- |
| `RTT_UDS_SERVICE_NODE_INIT(...)` | 初始化运行期服务节点 |
| `RTT_UDS_SERVICE_DEFINE(...)` | 定义静态服务节点 |
| `RTT_UDS_SERVICE_DECLARE(name)` | 声明 register / unregister 包装函数 |
| `RTT_UDS_SERVICE_DEFINE_OPS_PRO(...)` | 生成带显式优先级与上下文的包装函数 |
| `RTT_UDS_SERVICE_DEFINE_OPS(...)` | 使用默认优先级与空上下文的简化包装函数 |

## 9. 构建侧接口地图

| 文件 | 作用 |
| --- | --- |
| `client_demo/CMakeLists.txt` | Linux / Windows 构建分支选择 |
| `client_demo/CMakePresets.json` | Windows 命名 preset |
| `client_demo/Makefile` | Linux 兼容构建路径 |
| `client_demo/toolchain.cmake` | Yocto / 交叉编译入口 |
| `server_demo/Kconfig` | RT-Thread 功能开关 |
| `server_demo/SConscript` | RT-Thread 源码集成入口 |

## 10. 推荐扩展点

### 新增一个客户端服务命令

1. 在 `client_demo/services/` 下新增模块
2. 在 `main.c` 中注册
3. 尽量复用 `uds_context` 的事务辅助接口，不要直接绕开 transport

### 新增一个 transport 后端

1. 扩展 `uds_transport_backend_t`
2. 在 `transport.h` 里定义后端私有配置结构
3. 在 `client_demo/transport/` 中实现后端
4. 后端只处理 transport 事务，不要把 UDS 业务逻辑塞进去

### 新增一个 RT-Thread 服务处理器

1. 定义 `uds_service_node_t`
2. 通过 `rtt_uds_service_register()` 注册
3. 板级 I/O 尽量留在 handler 内，不要污染通用 RT-Thread UDS 核心
