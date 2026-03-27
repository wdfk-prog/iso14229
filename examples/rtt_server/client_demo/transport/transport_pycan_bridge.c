/**
 * @file transport_pycan_bridge.c
 * @brief Windows python-can sidecar backend for the transport abstraction.
 *
 * Design notes:
 * - Reuses iso14229's `UDSISOTpC_t`; this backend owns only raw CAN frame I/O.
 * - Python sidecar transport is local IPC carrying JSON Lines over child stdio
 *   (primary) or loopback TCP (debug-only reserve path).
 * - Incoming raw CAN frames are queued first, then drained from `poll()` and
 *   forwarded to `isotp_on_can_message(...)` before the original ISO-TP poll.
 */

#include "transport.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef UDS_TP_ISOTP_C
#error "pycan_bridge backend requires UDS_TP_ISOTP_C for Windows builds"
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define UDS_PYCAN_PROTOCOL_VERSION      "pycan-bridge/1"
#define UDS_PYCAN_CLIENT_NAME           "client_demo"
#define UDS_PYCAN_DEFAULT_TIMEOUT_MS    250U
#define UDS_PYCAN_CLOSE_GRACE_MS        300U
#define UDS_PYCAN_IO_POLL_GRANULARITY   10U
#define UDS_PYCAN_CMD_BUF_SIZE          1024U
#define UDS_PYCAN_LINE_BUF_SIZE         4096U
#define UDS_PYCAN_READ_BUF_SIZE         16384U
#define UDS_PYCAN_SCOPE_BUF_SIZE        32U
#define UDS_PYCAN_CODE_BUF_SIZE         64U
#define UDS_PYCAN_DETAIL_BUF_SIZE       256U
#define UDS_PYCAN_STR_SMALL             64U
#define UDS_PYCAN_STR_MEDIUM            128U
#define UDS_PYCAN_STR_LARGE             260U
#define UDS_PYCAN_MAX_FRAME_DATA        64U
#define UDS_PYCAN_MAX_HEX_DATA          (UDS_PYCAN_MAX_FRAME_DATA * 2U)
#define UDS_PYCAN_CAN_ID_MASK_STD       0x7FFU
#define UDS_PYCAN_CAN_ID_MASK_EXT       0x1FFFFFFFU

typedef struct {
    uint32_t can_id;
    uint8_t data[UDS_PYCAN_MAX_FRAME_DATA];
    uint8_t len;
    uint8_t is_fd;
} uds_pycan_rx_frame_t;

typedef struct {
    UDSISOTpC_t isotp;
    UDSTpStatus_t (*original_poll)(struct UDSTp *hdl);
    uds_transport_t *owner;

    CRITICAL_SECTION queue_lock;
    bool queue_lock_initialized;
    uds_pycan_rx_frame_t *rx_queue;
    uint32_t rx_queue_capacity;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
    bool rx_overflow;
    bool unsupported_fd_len_seen;
    bool line_overflow_seen;

    bool use_canfd;
    bool use_brs;
    bool use_extended_ids;
    bool auto_spawn;
    uds_transport_pycan_bridge_ipc_t ipc_mode;

    uint32_t phys_rx_can_id;
    uint32_t phys_tx_can_id;
    uint32_t func_tx_can_id;
    uint32_t bitrate;
    uint32_t open_timeout_ms;
    uint32_t io_timeout_ms;
    uint32_t next_seq;
    uint16_t port;

    char python_exe[UDS_PYCAN_STR_MEDIUM];
    char bridge_script[UDS_PYCAN_STR_LARGE];
    char interface_name[UDS_PYCAN_STR_SMALL];
    char channel_name[UDS_PYCAN_STR_MEDIUM];
    char host[UDS_PYCAN_STR_SMALL];

    HANDLE child_stdin_write;
    HANDLE child_stdout_read;
    HANDLE child_process;
    HANDLE child_thread;
    DWORD child_pid;
    bool child_spawned;

    bool winsock_started;
    SOCKET sock;
    bool io_connected;
    bool hello_done;
    bool bridge_opened;
    bool bridge_closed;
    bool peer_disconnected;

    char read_buf[UDS_PYCAN_READ_BUF_SIZE];
    size_t read_len;
    bool drop_until_newline;

    char last_scope[UDS_PYCAN_SCOPE_BUF_SIZE];
    char last_code[UDS_PYCAN_CODE_BUF_SIZE];
    char last_detail[UDS_PYCAN_DETAIL_BUF_SIZE];
} uds_transport_pycan_ctx_t;

_Static_assert(offsetof(uds_transport_pycan_ctx_t, isotp) == 0,
               "uds_transport_pycan_ctx_t.isotp must be at offset 0");
_Static_assert(offsetof(UDSISOTpC_t, hdl) == 0,
               "UDSISOTpC_t.hdl must be at offset 0");
_Static_assert(sizeof(uds_transport_pycan_ctx_t) <= UDS_TRANSPORT_STORAGE_CAPACITY,
               "UDS_TRANSPORT_STORAGE_CAPACITY is too small for pycan backend context");

static UDSTpStatus_t pycan_intercepted_poll(struct UDSTp *hdl);

static uds_transport_pycan_ctx_t *pycan_ctx(uds_transport_t *tp)
{
    return (uds_transport_pycan_ctx_t *)tp->backend_ctx;
}

static void pycan_report_async_error(uds_transport_pycan_ctx_t *ctx,
                                     uds_transport_async_error_t err)
{
    if (ctx == NULL || ctx->owner == NULL || ctx->owner->err_cb == NULL) {
        return;
    }

    ctx->owner->err_cb(ctx->owner->err_user, err);
}

static int pycan_record_error(uds_transport_t *tp,
                              uds_transport_pycan_ctx_t *ctx,
                              int err)
{
    if (tp != NULL) {
        tp->last_error = err;
    }
    (void)ctx;
    return -1;
}

static uint32_t pycan_now_ms(void)
{
    return (uint32_t)GetTickCount64();
}

static void pycan_copy_string(char *dst, size_t dst_size, const char *src, const char *fallback)
{
    const char *chosen = src;

    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (chosen == NULL || chosen[0] == '\0') {
        chosen = fallback;
    }
    if (chosen == NULL) {
        chosen = "";
    }

    snprintf(dst, dst_size, "%s", chosen);
}

static uint32_t pycan_mask_can_id(uint32_t can_id, bool extended)
{
    return can_id & (extended ? UDS_PYCAN_CAN_ID_MASK_EXT : UDS_PYCAN_CAN_ID_MASK_STD);
}

static size_t pycan_align_up(size_t value, size_t align)
{
    return (value + (align - 1U)) & ~(align - 1U);
}

static void pycan_queue_reset(uds_transport_pycan_ctx_t *ctx)
{
    if (ctx == NULL || ctx->rx_queue == NULL || ctx->rx_queue_capacity == 0U) {
        return;
    }

    memset(ctx->rx_queue, 0, sizeof(ctx->rx_queue[0]) * ctx->rx_queue_capacity);
    ctx->rx_head = 0U;
    ctx->rx_tail = 0U;
    ctx->rx_count = 0U;
    ctx->rx_overflow = false;
    ctx->unsupported_fd_len_seen = false;
    ctx->line_overflow_seen = false;
}

static int pycan_queue_push(uds_transport_pycan_ctx_t *ctx,
                            uint32_t can_id,
                            const uint8_t *data,
                            uint8_t len,
                            bool is_fd)
{
    uds_pycan_rx_frame_t *slot;

    if (ctx == NULL || data == NULL || len == 0U || len > UDS_PYCAN_MAX_FRAME_DATA ||
        ctx->rx_queue == NULL || ctx->rx_queue_capacity == 0U) {
        return -1;
    }

    if (ctx->queue_lock_initialized) {
        EnterCriticalSection(&ctx->queue_lock);
    }

    if (ctx->rx_count >= ctx->rx_queue_capacity) {
        ctx->rx_overflow = true;
        if (ctx->queue_lock_initialized) {
            LeaveCriticalSection(&ctx->queue_lock);
        }
        return -1;
    }

    slot = &ctx->rx_queue[ctx->rx_tail];
    memset(slot, 0, sizeof(*slot));
    slot->can_id = can_id;
    slot->len = len;
    slot->is_fd = is_fd ? 1U : 0U;
    memcpy(slot->data, data, len);

    ctx->rx_tail = (ctx->rx_tail + 1U) % ctx->rx_queue_capacity;
    ctx->rx_count++;

    if (ctx->queue_lock_initialized) {
        LeaveCriticalSection(&ctx->queue_lock);
    }

    return 0;
}

static int pycan_queue_pop(uds_transport_pycan_ctx_t *ctx, uds_pycan_rx_frame_t *out)
{
    if (ctx == NULL || out == NULL || ctx->rx_queue == NULL || ctx->rx_queue_capacity == 0U) {
        return -1;
    }

    if (ctx->queue_lock_initialized) {
        EnterCriticalSection(&ctx->queue_lock);
    }

    if (ctx->rx_count == 0U) {
        if (ctx->queue_lock_initialized) {
            LeaveCriticalSection(&ctx->queue_lock);
        }
        return 0;
    }

    *out = ctx->rx_queue[ctx->rx_head];
    memset(&ctx->rx_queue[ctx->rx_head], 0, sizeof(ctx->rx_queue[ctx->rx_head]));
    ctx->rx_head = (ctx->rx_head + 1U) % ctx->rx_queue_capacity;
    ctx->rx_count--;

    if (ctx->queue_lock_initialized) {
        LeaveCriticalSection(&ctx->queue_lock);
    }

    return 1;
}

static void pycan_feed_rx_queue(uds_transport_pycan_ctx_t *ctx)
{
    uds_pycan_rx_frame_t frame;

    if (ctx == NULL) {
        return;
    }

    while (pycan_queue_pop(ctx, &frame) > 0) {
        if (frame.can_id != ctx->phys_rx_can_id) {
            continue;
        }

        if (frame.is_fd && frame.len > 8U) {
            ctx->unsupported_fd_len_seen = true;
            continue;
        }

        isotp_on_can_message(&ctx->isotp.phys_link, frame.data, frame.len);
    }
}

static void pycan_set_last_protocol_error(uds_transport_pycan_ctx_t *ctx,
                                          const char *scope,
                                          const char *code,
                                          const char *detail)
{
    if (ctx == NULL) {
        return;
    }

    pycan_copy_string(ctx->last_scope, sizeof(ctx->last_scope), scope, "");
    pycan_copy_string(ctx->last_code, sizeof(ctx->last_code), code, "");
    pycan_copy_string(ctx->last_detail, sizeof(ctx->last_detail), detail, "");
}

static void pycan_close_socket(uds_transport_pycan_ctx_t *ctx)
{
    if (ctx != NULL && ctx->sock != INVALID_SOCKET) {
        closesocket(ctx->sock);
        ctx->sock = INVALID_SOCKET;
    }
}

static void pycan_close_child_handles(uds_transport_pycan_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->child_stdin_write != NULL) {
        CloseHandle(ctx->child_stdin_write);
        ctx->child_stdin_write = NULL;
    }
    if (ctx->child_stdout_read != NULL) {
        CloseHandle(ctx->child_stdout_read);
        ctx->child_stdout_read = NULL;
    }
    if (ctx->child_thread != NULL) {
        CloseHandle(ctx->child_thread);
        ctx->child_thread = NULL;
    }
    if (ctx->child_process != NULL) {
        CloseHandle(ctx->child_process);
        ctx->child_process = NULL;
    }
    ctx->child_pid = 0U;
}

static void pycan_shutdown_ctx(uds_transport_pycan_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->original_poll != NULL) {
        ctx->isotp.hdl.poll = ctx->original_poll;
    }
    ctx->original_poll = NULL;

    if (ctx->io_connected) {
        char line[128];
        DWORD wait_ms = UDS_PYCAN_CLOSE_GRACE_MS;

        snprintf(line, sizeof(line),
                 "{\"type\":\"close\",\"seq\":%lu,\"reason\":\"client_shutdown\"}\n",
                 (unsigned long)(++ctx->next_seq));
        if (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL && ctx->child_stdin_write != NULL) {
            DWORD ignored = 0U;
            (void)WriteFile(ctx->child_stdin_write, line, (DWORD)strlen(line), &ignored, NULL);
            FlushFileBuffers(ctx->child_stdin_write);
        } else if (ctx->sock != INVALID_SOCKET) {
            (void)send(ctx->sock, line, (int)strlen(line), 0);
            shutdown(ctx->sock, SD_BOTH);
        }

        if (ctx->child_process != NULL && ctx->child_spawned) {
            DWORD wait_rc = WaitForSingleObject(ctx->child_process, wait_ms);
            if (wait_rc == WAIT_TIMEOUT) {
                TerminateProcess(ctx->child_process, 1U);
                WaitForSingleObject(ctx->child_process, wait_ms);
            }
        }
    } else if (ctx->child_process != NULL && ctx->child_spawned) {
        TerminateProcess(ctx->child_process, 1U);
        WaitForSingleObject(ctx->child_process, UDS_PYCAN_CLOSE_GRACE_MS);
    }

    pycan_close_socket(ctx);
    pycan_close_child_handles(ctx);

    if (ctx->winsock_started) {
        WSACleanup();
        ctx->winsock_started = false;
    }

    if (ctx->queue_lock_initialized) {
        DeleteCriticalSection(&ctx->queue_lock);
        ctx->queue_lock_initialized = false;
    }

    ctx->owner = NULL;
    ctx->io_connected = false;
    ctx->hello_done = false;
    ctx->bridge_opened = false;
    ctx->bridge_closed = true;
    ctx->peer_disconnected = true;
}

static const char *pycan_skip_ws(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static const char *pycan_find_json_value(const char *json, const char *key)
{
    char pattern[64];
    const char *p;
    size_t key_len;

    if (json == NULL || key == NULL) {
        return NULL;
    }

    key_len = strlen(key);
    if (key_len + 4U >= sizeof(pattern)) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (p == NULL) {
        return NULL;
    }

    p += strlen(pattern);
    p = pycan_skip_ws(p);
    if (p == NULL || *p != ':') {
        return NULL;
    }
    ++p;
    return pycan_skip_ws(p);
}

static int pycan_json_get_u32(const char *json, const char *key, uint32_t *out)
{
    const char *p;
    char *endp = NULL;
    unsigned long value;

    if (out == NULL) {
        return -1;
    }

    p = pycan_find_json_value(json, key);
    if (p == NULL) {
        return -1;
    }

    value = strtoul(p, &endp, 10);
    if (endp == p) {
        return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static int pycan_json_get_bool(const char *json, const char *key, bool *out)
{
    const char *p;

    if (out == NULL) {
        return -1;
    }

    p = pycan_find_json_value(json, key);
    if (p == NULL) {
        return -1;
    }

    if (strncmp(p, "true", 4U) == 0) {
        *out = true;
        return 0;
    }
    if (strncmp(p, "false", 5U) == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int pycan_json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    const char *p;
    size_t n = 0U;

    if (out == NULL || out_size == 0U) {
        return -1;
    }

    p = pycan_find_json_value(json, key);
    if (p == NULL || *p != '"') {
        return -1;
    }

    ++p;
    while (*p != '\0') {
        if (*p == '"') {
            out[n] = '\0';
            return 0;
        }

        if (*p == '\\') {
            ++p;
            if (*p == '\0') {
                break;
            }
            switch (*p) {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    if (n + 1U >= out_size) { return -1; }
                    out[n++] = '\b';
                    ++p;
                    continue;
                case 'f':
                    if (n + 1U >= out_size) { return -1; }
                    out[n++] = '\f';
                    ++p;
                    continue;
                case 'n':
                    if (n + 1U >= out_size) { return -1; }
                    out[n++] = '\n';
                    ++p;
                    continue;
                case 'r':
                    if (n + 1U >= out_size) { return -1; }
                    out[n++] = '\r';
                    ++p;
                    continue;
                case 't':
                    if (n + 1U >= out_size) { return -1; }
                    out[n++] = '\t';
                    ++p;
                    continue;
                default:
                    break;
            }
        }

        if (n + 1U >= out_size) {
            return -1;
        }
        out[n++] = *p++;
    }

    return -1;
}

static int pycan_hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (int)(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (int)(ch - 'A' + 10);
    }
    return -1;
}

static int pycan_decode_hex(const char *hex, uint8_t *out, size_t out_cap, uint8_t *out_len)
{
    size_t hex_len;
    size_t i;

    if (hex == NULL || out == NULL || out_len == NULL) {
        return -1;
    }

    hex_len = strlen(hex);
    if ((hex_len % 2U) != 0U) {
        return -1;
    }
    if ((hex_len / 2U) > out_cap) {
        return -1;
    }

    for (i = 0U; i < hex_len; i += 2U) {
        int hi = pycan_hex_nibble(hex[i]);
        int lo = pycan_hex_nibble(hex[i + 1U]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2U] = (uint8_t)((hi << 4) | lo);
    }

    *out_len = (uint8_t)(hex_len / 2U);
    return 0;
}

static void pycan_encode_hex(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    static const char k_hex[] = "0123456789abcdef";
    size_t i;

    if (out == NULL || out_size == 0U) {
        return;
    }

    if (data == NULL || (len * 2U) + 1U > out_size) {
        out[0] = '\0';
        return;
    }

    for (i = 0U; i < len; ++i) {
        out[i * 2U] = k_hex[(data[i] >> 4) & 0x0FU];
        out[i * 2U + 1U] = k_hex[data[i] & 0x0FU];
    }
    out[len * 2U] = '\0';
}

static int pycan_append_read_data(uds_transport_pycan_ctx_t *ctx, const char *data, size_t len)
{
    size_t i;

    if (ctx == NULL || data == NULL || len == 0U) {
        return 0;
    }

    for (i = 0U; i < len; ++i) {
        char ch = data[i];

        if (ctx->drop_until_newline) {
            if (ch == '\n') {
                ctx->drop_until_newline = false;
            }
            continue;
        }

        if (ctx->read_len + 1U >= sizeof(ctx->read_buf)) {
            ctx->line_overflow_seen = true;
            ctx->drop_until_newline = true;
            continue;
        }

        ctx->read_buf[ctx->read_len++] = ch;
    }

    if (ctx->read_len < sizeof(ctx->read_buf)) {
        ctx->read_buf[ctx->read_len] = '\0';
    }
    return 0;
}

static int pycan_try_extract_line(uds_transport_pycan_ctx_t *ctx, char *out, size_t out_size)
{
    char *newline;
    size_t line_len;
    size_t remain;

    if (ctx == NULL || out == NULL || out_size == 0U) {
        return -1;
    }

    if (ctx->read_len == 0U) {
        return 0;
    }

    newline = memchr(ctx->read_buf, '\n', ctx->read_len);
    if (newline == NULL) {
        return 0;
    }

    line_len = (size_t)(newline - ctx->read_buf);
    while (line_len > 0U && (ctx->read_buf[line_len - 1U] == '\r' || ctx->read_buf[line_len - 1U] == '\n')) {
        --line_len;
    }

    if (line_len + 1U > out_size) {
        return -1;
    }

    memcpy(out, ctx->read_buf, line_len);
    out[line_len] = '\0';

    remain = ctx->read_len - ((size_t)(newline - ctx->read_buf) + 1U);
    memmove(ctx->read_buf, newline + 1, remain);
    ctx->read_len = remain;
    if (ctx->read_len < sizeof(ctx->read_buf)) {
        ctx->read_buf[ctx->read_len] = '\0';
    }
    return 1;
}

static int pycan_pipe_read_some(uds_transport_pycan_ctx_t *ctx, uint32_t wait_ms)
{
    uint32_t deadline = pycan_now_ms() + wait_ms;

    if (ctx == NULL || ctx->child_stdout_read == NULL) {
        return -1;
    }

    do {
        DWORD avail = 0U;
        DWORD bytes_read = 0U;
        char tmp[512];

        if (!PeekNamedPipe(ctx->child_stdout_read, NULL, 0U, NULL, &avail, NULL)) {
            return -1;
        }

        if (avail > 0U) {
            DWORD to_read = (avail < sizeof(tmp)) ? avail : (DWORD)sizeof(tmp);
            if (!ReadFile(ctx->child_stdout_read, tmp, to_read, &bytes_read, NULL) || bytes_read == 0U) {
                return -1;
            }
            return pycan_append_read_data(ctx, tmp, bytes_read);
        }

        if (ctx->child_process != NULL) {
            DWORD wait_rc = WaitForSingleObject(ctx->child_process, 0U);
            if (wait_rc == WAIT_OBJECT_0) {
                return -1;
            }
        }

        if (wait_ms == 0U) {
            return 0;
        }
        Sleep(UDS_PYCAN_IO_POLL_GRANULARITY);
    } while (pycan_now_ms() < deadline);

    return 0;
}

static int pycan_socket_send_all(SOCKET sock, const char *buf, size_t len, uint32_t timeout_ms)
{
    uint32_t deadline = pycan_now_ms() + timeout_ms;
    size_t sent_total = 0U;

    while (sent_total < len) {
        int n = send(sock, buf + sent_total, (int)(len - sent_total), 0);
        if (n > 0) {
            sent_total += (size_t)n;
            continue;
        }

        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wfds;
            struct timeval tv;
            long remain = (long)((deadline > pycan_now_ms()) ? (deadline - pycan_now_ms()) : 0U);
            if (remain <= 0L) {
                return -1;
            }
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            tv.tv_sec = remain / 1000L;
            tv.tv_usec = (remain % 1000L) * 1000L;
            if (select(0, NULL, &wfds, NULL, &tv) <= 0) {
                return -1;
            }
            continue;
        }
        return -1;
    }

    return 0;
}

static int pycan_socket_read_some(uds_transport_pycan_ctx_t *ctx, uint32_t wait_ms)
{
    fd_set rfds;
    struct timeval tv;
    int rc;
    char tmp[512];
    int n;

    if (ctx == NULL || ctx->sock == INVALID_SOCKET) {
        return -1;
    }

    FD_ZERO(&rfds);
    FD_SET(ctx->sock, &rfds);
    tv.tv_sec = wait_ms / 1000U;
    tv.tv_usec = (wait_ms % 1000U) * 1000U;

    rc = select(0, &rfds, NULL, NULL, &tv);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0) {
        return -1;
    }

    n = recv(ctx->sock, tmp, (int)sizeof(tmp), 0);
    if (n <= 0) {
        return -1;
    }

    return pycan_append_read_data(ctx, tmp, (size_t)n);
}

static int pycan_read_some(uds_transport_pycan_ctx_t *ctx, uint32_t wait_ms)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL) {
        return pycan_pipe_read_some(ctx, wait_ms);
    }
    return pycan_socket_read_some(ctx, wait_ms);
}

static int pycan_next_line(uds_transport_pycan_ctx_t *ctx, char *out, size_t out_size, uint32_t wait_ms)
{
    uint32_t deadline = pycan_now_ms() + wait_ms;

    if (ctx == NULL || out == NULL || out_size == 0U) {
        return -1;
    }

    for (;;) {
        int rc = pycan_try_extract_line(ctx, out, out_size);
        if (rc != 0) {
            return rc;
        }

        rc = pycan_read_some(ctx, wait_ms == 0U ? 0U : UDS_PYCAN_IO_POLL_GRANULARITY);
        if (rc < 0) {
            return -1;
        }

        if (wait_ms == 0U) {
            return pycan_try_extract_line(ctx, out, out_size);
        }
        if (pycan_now_ms() >= deadline) {
            return pycan_try_extract_line(ctx, out, out_size);
        }
    }
}

static int pycan_send_line(uds_transport_pycan_ctx_t *ctx, const char *line)
{
    if (ctx == NULL || line == NULL) {
        return -1;
    }

    if (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL) {
        const char *p = line;
        size_t remain = strlen(line);

        if (ctx->child_stdin_write == NULL) {
            return -1;
        }

        while (remain > 0U) {
            DWORD written = 0U;
            DWORD chunk = (remain > 4096U) ? 4096U : (DWORD)remain;
            if (!WriteFile(ctx->child_stdin_write, p, chunk, &written, NULL) || written == 0U) {
                return -1;
            }
            remain -= written;
            p += written;
        }
        FlushFileBuffers(ctx->child_stdin_write);
        return 0;
    }

    if (ctx->sock == INVALID_SOCKET) {
        return -1;
    }

    return pycan_socket_send_all(ctx->sock,
                                 line,
                                 strlen(line),
                                 (ctx->io_timeout_ms > 0U) ? ctx->io_timeout_ms : UDS_PYCAN_DEFAULT_TIMEOUT_MS);
}

static int pycan_handle_rx_message(uds_transport_pycan_ctx_t *ctx, const char *line)
{
    uint32_t can_id;
    bool is_fd = false;
    char data_hex[UDS_PYCAN_MAX_HEX_DATA + 1U];
    uint8_t data[UDS_PYCAN_MAX_FRAME_DATA];
    uint8_t len = 0U;

    if (pycan_json_get_u32(line, "can_id", &can_id) != 0) {
        return -1;
    }
    if (pycan_json_get_bool(line, "fd", &is_fd) != 0) {
        is_fd = false;
    }
    if (pycan_json_get_string(line, "data", data_hex, sizeof(data_hex)) != 0) {
        return -1;
    }
    if (pycan_decode_hex(data_hex, data, sizeof(data), &len) != 0) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }

    (void)pycan_queue_push(ctx,
                           pycan_mask_can_id(can_id, ctx->use_extended_ids),
                           data,
                           len,
                           is_fd);
    return 0;
}

static int pycan_process_line(uds_transport_pycan_ctx_t *ctx,
                              const char *line,
                              uint32_t wait_reply_to,
                              const char *expected_type,
                              bool *matched)
{
    char type[32];
    uint32_t reply_to = 0U;
    bool has_reply_to = (pycan_json_get_u32(line, "reply_to", &reply_to) == 0);

    if (ctx == NULL || line == NULL) {
        return -1;
    }
    if (matched != NULL) {
        *matched = false;
    }

    if (pycan_json_get_string(line, "type", type, sizeof(type)) != 0) {
        pycan_set_last_protocol_error(ctx, "protocol", "INVALID_MESSAGE", "missing or invalid type");
        return -1;
    }

    if (strcmp(type, "rx") == 0) {
        if (pycan_handle_rx_message(ctx, line) != 0) {
            pycan_set_last_protocol_error(ctx, "recv", "INVALID_MESSAGE", "invalid rx payload");
            return -1;
        }
        return 0;
    }

    if (strcmp(type, "error") == 0) {
        char scope[UDS_PYCAN_SCOPE_BUF_SIZE];
        char code[UDS_PYCAN_CODE_BUF_SIZE];
        char detail[UDS_PYCAN_DETAIL_BUF_SIZE];

        scope[0] = '\0';
        code[0] = '\0';
        detail[0] = '\0';
        (void)pycan_json_get_string(line, "scope", scope, sizeof(scope));
        (void)pycan_json_get_string(line, "code", code, sizeof(code));
        (void)pycan_json_get_string(line, "detail", detail, sizeof(detail));
        pycan_set_last_protocol_error(ctx, scope, code, detail);

        if ((strcmp(code, "BUS_RECV_FAILED") == 0) || (strcmp(code, "BUS_DISCONNECTED") == 0)) {
            ctx->peer_disconnected = true;
            pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_DISCONNECTED);
        } else {
            pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
        }

        if (has_reply_to && reply_to == wait_reply_to) {
            return -1;
        }
        return 0;
    }

    if (strcmp(type, "closed") == 0) {
        ctx->bridge_closed = true;
        ctx->peer_disconnected = true;
        pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_DISCONNECTED);
        if (expected_type != NULL && strcmp(expected_type, "closed") == 0 && has_reply_to && reply_to == wait_reply_to) {
            if (matched != NULL) {
                *matched = true;
            }
            return 0;
        }
        return -1;
    }

    if (strcmp(type, "hello") == 0) {
        ctx->hello_done = true;
    } else if (strcmp(type, "opened") == 0) {
        ctx->bridge_opened = true;
    }

    if (expected_type != NULL && strcmp(type, expected_type) == 0 &&
        ((wait_reply_to == 0U) || (has_reply_to && reply_to == wait_reply_to))) {
        if (matched != NULL) {
            *matched = true;
        }
    }
    return 0;
}

static int pycan_wait_for_reply(uds_transport_pycan_ctx_t *ctx,
                                uint32_t reply_to,
                                const char *expected_type,
                                uint32_t timeout_ms)
{
    uint32_t deadline = pycan_now_ms() + timeout_ms;
    char line[UDS_PYCAN_LINE_BUF_SIZE];

    if (ctx == NULL || expected_type == NULL) {
        return -1;
    }

    while (pycan_now_ms() <= deadline) {
        bool matched = false;
        int rc = pycan_next_line(ctx,
                                 line,
                                 sizeof(line),
                                 (timeout_ms == 0U) ? 0U : UDS_PYCAN_IO_POLL_GRANULARITY);
        if (rc < 0) {
            ctx->peer_disconnected = true;
            pycan_set_last_protocol_error(ctx, "io", "BUS_DISCONNECTED", "bridge EOF or IPC failure");
            return -1;
        }
        if (rc == 0) {
            continue;
        }

        rc = pycan_process_line(ctx, line, reply_to, expected_type, &matched);
        if (rc < 0) {
            return -1;
        }
        if (matched) {
            return 0;
        }
    }

    pycan_set_last_protocol_error(ctx, "timeout", "TIMEOUT", expected_type);
    return -1;
}


static int pycan_wait_for_tx_result(uds_transport_pycan_ctx_t *ctx,
                                    uint32_t reply_to,
                                    uint32_t timeout_ms)
{
    uint32_t deadline = pycan_now_ms() + timeout_ms;
    char line[UDS_PYCAN_LINE_BUF_SIZE];

    if (ctx == NULL) {
        return -1;
    }

    while (pycan_now_ms() <= deadline) {
        char type[32];
        uint32_t event_reply_to = 0U;
        bool has_reply_to;
        bool ignored_match = false;
        int rc = pycan_next_line(ctx,
                                 line,
                                 sizeof(line),
                                 (timeout_ms == 0U) ? 0U : UDS_PYCAN_IO_POLL_GRANULARITY);
        if (rc < 0) {
            ctx->peer_disconnected = true;
            pycan_set_last_protocol_error(ctx, "io", "BUS_DISCONNECTED", "bridge EOF or IPC failure");
            return -1;
        }
        if (rc == 0) {
            continue;
        }

        if (pycan_json_get_string(line, "type", type, sizeof(type)) == 0) {
            has_reply_to = (pycan_json_get_u32(line, "reply_to", &event_reply_to) == 0);
            if (has_reply_to && event_reply_to == reply_to) {
                if (strcmp(type, "tx_done") == 0) {
                    return 0;
                }
                if (strcmp(type, "error") == 0) {
                    if (pycan_process_line(ctx, line, reply_to, "tx_done", &ignored_match) != 0) {
                        return -1;
                    }
                }
            }
        }

        if (pycan_process_line(ctx, line, 0U, NULL, &ignored_match) != 0) {
            return -1;
        }
    }

    return 0;
}

static int pycan_pump_io(uds_transport_pycan_ctx_t *ctx)
{
    char line[UDS_PYCAN_LINE_BUF_SIZE];

    if (ctx == NULL) {
        return -1;
    }

    for (;;) {
        bool ignored_match = false;
        int rc = pycan_next_line(ctx, line, sizeof(line), 0U);
        if (rc < 0) {
            ctx->peer_disconnected = true;
            pycan_set_last_protocol_error(ctx, "io", "BUS_DISCONNECTED", "bridge EOF or IPC failure");
            return -1;
        }
        if (rc == 0) {
            break;
        }
        if (pycan_process_line(ctx, line, 0U, NULL, &ignored_match) != 0) {
            return -1;
        }
    }

    return 0;
}

static int pycan_start_winsock(uds_transport_pycan_ctx_t *ctx)
{
    WSADATA wsa_data;

    if (ctx == NULL) {
        return -1;
    }
    if (ctx->winsock_started) {
        return 0;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return -1;
    }
    ctx->winsock_started = true;
    return 0;
}

static int pycan_connect_tcp(uds_transport_pycan_ctx_t *ctx, uint32_t timeout_ms)
{
    struct sockaddr_in addr;
    uint32_t deadline = pycan_now_ms() + timeout_ms;

    if (ctx == NULL) {
        return -1;
    }
    if (pycan_start_winsock(ctx) != 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->port);
    if (inet_pton(AF_INET, ctx->host, &addr.sin_addr) != 1) {
        return -1;
    }

    while (pycan_now_ms() <= deadline) {
        u_long nonblocking = 1UL;
        int rc;

        pycan_close_socket(ctx);
        ctx->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ctx->sock == INVALID_SOCKET) {
            return -1;
        }
        (void)ioctlsocket(ctx->sock, FIONBIO, &nonblocking);

        rc = connect(ctx->sock, (const struct sockaddr *)&addr, sizeof(addr));
        if (rc == 0) {
            ctx->io_connected = true;
            return 0;
        }

        if (WSAGetLastError() == WSAEWOULDBLOCK ||
            WSAGetLastError() == WSAEINPROGRESS ||
            WSAGetLastError() == WSAEINVAL) {
            fd_set wfds;
            struct timeval tv;
            int sel;
            int so_error = 0;
            int so_len = sizeof(so_error);
            long remain = (long)((deadline > pycan_now_ms()) ? (deadline - pycan_now_ms()) : 0U);
            if (remain <= 0L) {
                break;
            }

            FD_ZERO(&wfds);
            FD_SET(ctx->sock, &wfds);
            tv.tv_sec = remain / 1000L;
            tv.tv_usec = (remain % 1000L) * 1000L;
            sel = select(0, NULL, &wfds, NULL, &tv);
            if (sel > 0 && getsockopt(ctx->sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_len) == 0 && so_error == 0) {
                ctx->io_connected = true;
                return 0;
            }
        }

        Sleep(50U);
    }

    pycan_close_socket(ctx);
    return -1;
}

static int pycan_quote_cmd_arg(char *dst, size_t dst_size, size_t *offset, const char *arg)
{
    const char *p;
    size_t off;

    if (dst == NULL || dst_size == 0U || offset == NULL || arg == NULL) {
        return -1;
    }

    off = *offset;
    if (off >= dst_size) {
        return -1;
    }
    if (off != 0U) {
        if (off + 1U >= dst_size) {
            return -1;
        }
        dst[off++] = ' ';
    }
    if (off + 2U >= dst_size) {
        return -1;
    }
    dst[off++] = '"';
    for (p = arg; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            if (off + 2U >= dst_size) {
                return -1;
            }
            dst[off++] = '\\';
        } else if (off + 1U >= dst_size) {
            return -1;
        }
        dst[off++] = *p;
    }
    if (off + 2U >= dst_size) {
        return -1;
    }
    dst[off++] = '"';
    dst[off] = '\0';
    *offset = off;
    return 0;
}

static int pycan_spawn_bridge(uds_transport_pycan_ctx_t *ctx)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE parent_stdout_read = NULL;
    HANDLE child_stdout_write = NULL;
    HANDLE child_stdin_read = NULL;
    HANDLE parent_stdin_write = NULL;
    HANDLE child_stderr = NULL;
    HANDLE parent_process = GetCurrentProcess();
    char cmdline[1024];
    size_t off = 0U;

    if (ctx == NULL) {
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&parent_stdout_read, &child_stdout_write, &sa, 0U)) {
        goto fail;
    }
    if (!SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0U)) {
        goto fail;
    }
    if (!CreatePipe(&child_stdin_read, &parent_stdin_write, &sa, 0U)) {
        goto fail;
    }
    if (!SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0U)) {
        goto fail;
    }

    if (!DuplicateHandle(parent_process,
                         GetStdHandle(STD_ERROR_HANDLE),
                         parent_process,
                         &child_stderr,
                         0U,
                         TRUE,
                         DUPLICATE_SAME_ACCESS)) {
        child_stderr = GetStdHandle(STD_ERROR_HANDLE);
    }

    memset(cmdline, 0, sizeof(cmdline));
    if (pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, ctx->python_exe) != 0 ||
        pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, "-u") != 0 ||
        pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, ctx->bridge_script) != 0 ||
        pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, "--ipc") != 0 ||
        pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off,
                            (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL) ? "stdio" : "tcp") != 0) {
        goto fail;
    }

    if (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_TCP_JSONL) {
        char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)ctx->port);
        if (pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, "--tcp-host") != 0 ||
            pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, ctx->host) != 0 ||
            pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, "--tcp-port") != 0 ||
            pycan_quote_cmd_arg(cmdline, sizeof(cmdline), &off, port_buf) != 0) {
            goto fail;
        }
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_STDIO_JSONL) ? child_stdout_write : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = (child_stderr != NULL && child_stderr != INVALID_HANDLE_VALUE) ? child_stderr : GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(NULL,
                        cmdline,
                        NULL,
                        NULL,
                        TRUE,
                        CREATE_NO_WINDOW,
                        NULL,
                        NULL,
                        &si,
                        &pi)) {
        goto fail;
    }

    CloseHandle(child_stdout_write);
    child_stdout_write = NULL;
    CloseHandle(child_stdin_read);
    child_stdin_read = NULL;
    if (child_stderr != NULL && child_stderr != INVALID_HANDLE_VALUE && child_stderr != GetStdHandle(STD_ERROR_HANDLE)) {
        CloseHandle(child_stderr);
        child_stderr = NULL;
    }

    ctx->child_stdout_read = parent_stdout_read;
    ctx->child_stdin_write = parent_stdin_write;
    ctx->child_process = pi.hProcess;
    ctx->child_thread = pi.hThread;
    ctx->child_pid = pi.dwProcessId;
    ctx->child_spawned = true;
    return 0;

fail:
    if (parent_stdout_read != NULL) {
        CloseHandle(parent_stdout_read);
    }
    if (child_stdout_write != NULL) {
        CloseHandle(child_stdout_write);
    }
    if (child_stdin_read != NULL) {
        CloseHandle(child_stdin_read);
    }
    if (parent_stdin_write != NULL) {
        CloseHandle(parent_stdin_write);
    }
    if (child_stderr != NULL && child_stderr != INVALID_HANDLE_VALUE && child_stderr != GetStdHandle(STD_ERROR_HANDLE)) {
        CloseHandle(child_stderr);
    }
    return -1;
}

static int pycan_send_hello(uds_transport_pycan_ctx_t *ctx)
{
    char line[UDS_PYCAN_CMD_BUF_SIZE];
    uint32_t seq;

    if (ctx == NULL) {
        return -1;
    }

    seq = ++ctx->next_seq;
    snprintf(line, sizeof(line),
             "{\"type\":\"hello\",\"seq\":%lu,\"version\":\"%s\",\"client\":\"%s\"}\n",
             (unsigned long)seq,
             UDS_PYCAN_PROTOCOL_VERSION,
             UDS_PYCAN_CLIENT_NAME);
    if (pycan_send_line(ctx, line) != 0) {
        return -1;
    }
    return pycan_wait_for_reply(ctx,
                                seq,
                                "hello",
                                (ctx->open_timeout_ms > 0U) ? ctx->open_timeout_ms : UDS_PYCAN_DEFAULT_TIMEOUT_MS);
}

static int pycan_send_open(uds_transport_pycan_ctx_t *ctx)
{
    char line[UDS_PYCAN_CMD_BUF_SIZE];
    uint32_t seq;

    if (ctx == NULL) {
        return -1;
    }

    seq = ++ctx->next_seq;
    snprintf(line, sizeof(line),
             "{\"type\":\"open\",\"seq\":%lu,\"interface\":\"%s\",\"channel\":\"%s\",\"bitrate\":%lu,\"fd\":%s,\"brs\":%s,\"extended_default\":%s,\"rx_queue_capacity\":%lu}\n",
             (unsigned long)seq,
             ctx->interface_name,
             ctx->channel_name,
             (unsigned long)ctx->bitrate,
             ctx->use_canfd ? "true" : "false",
             ctx->use_brs ? "true" : "false",
             ctx->use_extended_ids ? "true" : "false",
             (unsigned long)ctx->rx_queue_capacity);
    if (pycan_send_line(ctx, line) != 0) {
        return -1;
    }
    return pycan_wait_for_reply(ctx,
                                seq,
                                "opened",
                                (ctx->open_timeout_ms > 0U) ? ctx->open_timeout_ms : UDS_PYCAN_DEFAULT_TIMEOUT_MS);
}

static int pycan_send_tx_frame(uds_transport_pycan_ctx_t *ctx,
                               uint32_t arbitration_id,
                               const uint8_t *data,
                               uint8_t size)
{
    char hex[UDS_PYCAN_MAX_HEX_DATA + 1U];
    char line[UDS_PYCAN_CMD_BUF_SIZE];
    uint32_t seq;

    if (ctx == NULL || data == NULL || size == 0U || size > UDS_PYCAN_MAX_FRAME_DATA) {
        return -1;
    }

    pycan_encode_hex(data, size, hex, sizeof(hex));
    seq = ++ctx->next_seq;
    snprintf(line, sizeof(line),
             "{\"type\":\"tx\",\"seq\":%lu,\"can_id\":%lu,\"extended\":%s,\"fd\":%s,\"brs\":%s,\"rtr\":false,\"data\":\"%s\"}\n",
             (unsigned long)seq,
             (unsigned long)pycan_mask_can_id(arbitration_id, ctx->use_extended_ids),
             ctx->use_extended_ids ? "true" : "false",
             ctx->use_canfd ? "true" : "false",
             ctx->use_brs ? "true" : "false",
             hex);
    if (pycan_send_line(ctx, line) != 0) {
        return -1;
    }

    return pycan_wait_for_tx_result(ctx,
                                    seq,
                                    (ctx->io_timeout_ms > 0U) ? ctx->io_timeout_ms : UDS_PYCAN_DEFAULT_TIMEOUT_MS);
}

static int pycan_apply_runtime_cfg(uds_transport_pycan_ctx_t *ctx,
                                   const uds_transport_open_cfg_t *cfg)
{
    const uds_transport_pycan_bridge_cfg_t *backend_cfg;

    if (ctx == NULL || cfg == NULL || cfg->backend_cfg == NULL) {
        return -1;
    }

    backend_cfg = (const uds_transport_pycan_bridge_cfg_t *)cfg->backend_cfg;
    pycan_copy_string(ctx->python_exe, sizeof(ctx->python_exe), backend_cfg->python_exe, "python");
    pycan_copy_string(ctx->bridge_script, sizeof(ctx->bridge_script), backend_cfg->bridge_script, "tools/pycan_bridge.py");
    pycan_copy_string(ctx->interface_name, sizeof(ctx->interface_name), backend_cfg->interface_name, NULL);
    pycan_copy_string(ctx->channel_name, sizeof(ctx->channel_name), backend_cfg->channel_name, NULL);
    pycan_copy_string(ctx->host, sizeof(ctx->host), backend_cfg->host, "127.0.0.1");

    ctx->port = (backend_cfg->port != 0U) ? backend_cfg->port : 29536U;
    ctx->bitrate = backend_cfg->bitrate;
    ctx->open_timeout_ms = (backend_cfg->open_timeout_ms != 0U) ? backend_cfg->open_timeout_ms : 4000U;
    ctx->io_timeout_ms = (backend_cfg->io_timeout_ms != 0U) ? backend_cfg->io_timeout_ms : 250U;
    ctx->auto_spawn = backend_cfg->auto_spawn;
    ctx->use_canfd = backend_cfg->use_canfd;
    ctx->use_brs = backend_cfg->use_brs;
    ctx->use_extended_ids = backend_cfg->use_extended_ids;
    ctx->ipc_mode = backend_cfg->ipc_mode;

    if (ctx->interface_name[0] == '\0' || ctx->channel_name[0] == '\0' || ctx->bitrate == 0U) {
        return -1;
    }
    if (!ctx->auto_spawn && ctx->ipc_mode != UDS_PYCAN_BRIDGE_IPC_TCP_JSONL) {
        return -1;
    }

    ctx->phys_rx_can_id = pycan_mask_can_id(cfg->phys_sa, ctx->use_extended_ids);
    ctx->phys_tx_can_id = pycan_mask_can_id(cfg->phys_ta, ctx->use_extended_ids);
    ctx->func_tx_can_id = pycan_mask_can_id(cfg->func_sa, ctx->use_extended_ids);
    return 0;
}

static int pycan_open(uds_transport_t *tp, const uds_transport_open_cfg_t *cfg)
{
    uds_transport_pycan_ctx_t *ctx;
    const uds_transport_pycan_bridge_cfg_t *backend_cfg;
    UDSISOTpCConfig_t isotp_cfg;
    UDSErr_t uds_err;
    size_t queue_offset;
    size_t queue_slots;

    if (tp == NULL || cfg == NULL || cfg->backend_cfg == NULL ||
        cfg->backend != UDS_TRANSPORT_BACKEND_PYCAN_BRIDGE) {
        return -1;
    }
    if (tp->bound_storage == NULL || tp->bound_storage_size < sizeof(*ctx)) {
        tp->last_error = -1;
        return -1;
    }

    backend_cfg = (const uds_transport_pycan_bridge_cfg_t *)cfg->backend_cfg;
    ctx = (uds_transport_pycan_ctx_t *)tp->bound_storage;
    memset(ctx, 0, sizeof(*ctx));
    ctx->owner = tp;
    ctx->sock = INVALID_SOCKET;

    if (pycan_apply_runtime_cfg(ctx, cfg) != 0) {
        return pycan_record_error(tp, ctx, UDS_ERR_INVALID_ARG);
    }

    queue_offset = pycan_align_up(sizeof(*ctx), sizeof(void *));
    if (tp->bound_storage_size <= queue_offset) {
        return pycan_record_error(tp, ctx, UDS_ERR_BUFSIZ);
    }
    queue_slots = (tp->bound_storage_size - queue_offset) / sizeof(uds_pycan_rx_frame_t);
    if (queue_slots == 0U) {
        return pycan_record_error(tp, ctx, UDS_ERR_BUFSIZ);
    }
    if (backend_cfg->rx_queue_capacity != 0U && backend_cfg->rx_queue_capacity < queue_slots) {
        queue_slots = backend_cfg->rx_queue_capacity;
    }
    ctx->rx_queue = (uds_pycan_rx_frame_t *)((uint8_t *)tp->bound_storage + queue_offset);
    ctx->rx_queue_capacity = (uint32_t)queue_slots;

    InitializeCriticalSection(&ctx->queue_lock);
    ctx->queue_lock_initialized = true;
    pycan_queue_reset(ctx);

    if (ctx->auto_spawn) {
        if (pycan_spawn_bridge(ctx) != 0) {
            pycan_shutdown_ctx(ctx);
            return pycan_record_error(tp, ctx, UDS_ERR_TPORT);
        }
    }

    if (ctx->ipc_mode == UDS_PYCAN_BRIDGE_IPC_TCP_JSONL) {
        if (pycan_connect_tcp(ctx, ctx->open_timeout_ms) != 0) {
            pycan_shutdown_ctx(ctx);
            return pycan_record_error(tp, ctx, UDS_ERR_TPORT);
        }
    } else {
        if (ctx->child_stdin_write == NULL || ctx->child_stdout_read == NULL) {
            pycan_shutdown_ctx(ctx);
            return pycan_record_error(tp, ctx, UDS_ERR_TPORT);
        }
        ctx->io_connected = true;
    }

    if (pycan_send_hello(ctx) != 0) {
        pycan_shutdown_ctx(ctx);
        return pycan_record_error(tp, ctx, UDS_ERR_TPORT);
    }
    if (pycan_send_open(ctx) != 0) {
        pycan_shutdown_ctx(ctx);
        return pycan_record_error(tp, ctx, UDS_ERR_TPORT);
    }

    memset(&isotp_cfg, 0, sizeof(isotp_cfg));
    isotp_cfg.source_addr = cfg->phys_sa;
    isotp_cfg.target_addr = cfg->phys_ta;
    isotp_cfg.source_addr_func = cfg->phys_sa;
    isotp_cfg.target_addr_func = cfg->func_sa;
    uds_err = UDSISOTpCInit(&ctx->isotp, &isotp_cfg);
    if (uds_err != UDS_OK) {
        pycan_shutdown_ctx(ctx);
        return pycan_record_error(tp, ctx, (int)uds_err);
    }

    ctx->isotp.phys_link.user_send_can_arg = ctx;
    ctx->isotp.func_link.user_send_can_arg = ctx;
    ctx->original_poll = ctx->isotp.hdl.poll;
    ctx->isotp.hdl.poll = pycan_intercepted_poll;
    tp->backend_ctx = ctx;
    tp->last_error = 0;
    return 0;
}

static void pycan_close(uds_transport_t *tp)
{
    uds_transport_pycan_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return;
    }

    ctx = pycan_ctx(tp);
    pycan_shutdown_ctx(ctx);

    tp->backend_ctx = NULL;
    tp->last_error = 0;
}

static int pycan_send(uds_transport_t *tp,
                      const uint8_t *data,
                      size_t len,
                      bool functional)
{
    uds_transport_pycan_ctx_t *ctx;
    UDSSDU_t info;
    ssize_t n;

    if (tp == NULL || data == NULL || len == 0U || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = pycan_ctx(tp);
    memset(&info, 0, sizeof(info));
    info.A_TA_Type = functional ? UDS_A_TA_TYPE_FUNCTIONAL : UDS_A_TA_TYPE_PHYSICAL;

    n = UDSTpSend(&ctx->isotp.hdl, data, (ssize_t)len, &info);
    if (n < 0 || (size_t)n != len) {
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }
    return 0;
}

static UDSTpStatus_t pycan_intercepted_poll(struct UDSTp *hdl)
{
    uds_transport_pycan_ctx_t *ctx = (uds_transport_pycan_ctx_t *)hdl;
    UDSTpStatus_t status;

    if (ctx == NULL || ctx->original_poll == NULL) {
        return UDS_TP_ERR;
    }

    if (pycan_pump_io(ctx) != 0) {
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        return UDS_TP_ERR;
    }

    pycan_feed_rx_queue(ctx);
    status = ctx->original_poll(hdl);

    if (ctx->rx_overflow) {
        ctx->rx_overflow = false;
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
    }
    if (ctx->unsupported_fd_len_seen) {
        ctx->unsupported_fd_len_seen = false;
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
    }
    if (ctx->line_overflow_seen) {
        ctx->line_overflow_seen = false;
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        pycan_report_async_error(ctx, UDS_TRANSPORT_ASYNC_ERR_IO);
    }

    return status;
}

static int pycan_poll(uds_transport_t *tp)
{
    uds_transport_pycan_ctx_t *ctx;
    UDSTpStatus_t status;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return -1;
    }

    ctx = pycan_ctx(tp);
    status = UDSTpPoll(&ctx->isotp.hdl);
    if ((status & UDS_TP_ERR) != 0U || ctx->peer_disconnected) {
        tp->last_error = UDS_ERR_TPORT;
        return -1;
    }
    return 0;
}

static void pycan_set_timeout(uds_transport_t *tp, uint32_t timeout_ms)
{
    if (tp == NULL) {
        return;
    }
    tp->timeout_ms = timeout_ms;
}

static int pycan_get_last_error(uds_transport_t *tp)
{
    if (tp == NULL) {
        return -1;
    }
    return tp->last_error;
}

static UDSTp_t *pycan_get_tp_handle(uds_transport_t *tp)
{
    uds_transport_pycan_ctx_t *ctx;

    if (tp == NULL || tp->backend_ctx == NULL) {
        return NULL;
    }

    ctx = pycan_ctx(tp);
    return &ctx->isotp.hdl;
}

uint32_t isotp_user_get_us(void)
{
    return (uint32_t)(GetTickCount64() * 1000ULL);
}

void isotp_user_debug(const char *message, ...)
{
    (void)message;
}

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size,
                        void *arg)
{
    uds_transport_pycan_ctx_t *ctx = (uds_transport_pycan_ctx_t *)arg;

    if (ctx == NULL || data == NULL || size == 0U || size > UDS_PYCAN_MAX_FRAME_DATA) {
        return ISOTP_RET_ERROR;
    }
    if (!ctx->io_connected || ctx->peer_disconnected || ctx->bridge_closed) {
        return ISOTP_RET_ERROR;
    }
    if (pycan_send_tx_frame(ctx, arbitration_id, data, size) != 0) {
        if (ctx->owner != NULL) {
            ctx->owner->last_error = UDS_ERR_TPORT;
        }
        return ISOTP_RET_ERROR;
    }
    return ISOTP_RET_OK;
}

static const uds_transport_ops_t g_pycan_ops = {
    .open = pycan_open,
    .close = pycan_close,
    .send = pycan_send,
    .poll = pycan_poll,
    .set_timeout = pycan_set_timeout,
    .get_last_error = pycan_get_last_error,
    .get_tp_handle = pycan_get_tp_handle,
};

const uds_transport_ops_t *uds_transport_pycan_bridge_ops(void)
{
    return &g_pycan_ops;
}
