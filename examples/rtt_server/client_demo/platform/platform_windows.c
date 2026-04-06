/**
 * @file platform_windows.c
 * @brief Windows / MSYS2 MINGW64 implementation for interactive platform abstraction.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <io.h>

#include "platform.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE g_console_mode_in = INVALID_HANDLE_VALUE;
static HANDLE g_console_mode_out = INVALID_HANDLE_VALUE;
static DWORD g_console_in_mode = 0U;
static DWORD g_console_out_mode = 0U;
static int g_console_mode_saved = 0;
static int g_console_prepared = 0;
static int g_console_vt_enabled = 0;
static int g_console_restore_registered = 0;
static int g_console_modes_available = 0;

static HANDLE platform_open_console_handle(const char *name)
{
    return CreateFileA(name,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
}

static void platform_close_console_handle(HANDLE *handle)
{
    if (handle != NULL && *handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(*handle);
        *handle = INVALID_HANDLE_VALUE;
    }
}

static void platform_console_release_mode_handles(void)
{
    platform_close_console_handle(&g_console_mode_in);
    platform_close_console_handle(&g_console_mode_out);
}

static void platform_console_restore_once(void)
{
    if (g_console_mode_saved && g_console_modes_available) {
        if (g_console_mode_in != INVALID_HANDLE_VALUE) {
            (void)SetConsoleMode(g_console_mode_in, g_console_in_mode);
        }
        if (g_console_mode_out != INVALID_HANDLE_VALUE) {
            (void)SetConsoleMode(g_console_mode_out, g_console_out_mode);
        }
    }

    g_console_prepared = 0;
    g_console_vt_enabled = 0;
    g_console_mode_saved = 0;
    g_console_modes_available = 0;
    platform_console_release_mode_handles();
}

static void platform_console_restore_at_exit(void)
{
    platform_console_restore_once();
}

static int platform_console_capture_modes(void)
{
    if (g_console_mode_saved) {
        return 0;
    }
    if (g_console_prepared && !g_console_modes_available) {
        return 1;
    }

    g_console_mode_in = platform_open_console_handle("CONIN$");
    g_console_mode_out = platform_open_console_handle("CONOUT$");

    if (g_console_mode_in == INVALID_HANDLE_VALUE || g_console_mode_out == INVALID_HANDLE_VALUE) {
        platform_console_release_mode_handles();
        g_console_modes_available = 0;
        return 1;
    }

    if (!GetConsoleMode(g_console_mode_in, &g_console_in_mode) ||
        !GetConsoleMode(g_console_mode_out, &g_console_out_mode)) {
        platform_console_release_mode_handles();
        g_console_modes_available = 0;
        return 1;
    }

    g_console_modes_available = 1;
    g_console_mode_saved = 1;
    return 0;
}

uint32_t platform_tick_ms(void)
{
    return (uint32_t)GetTickCount64();
}

uint32_t UDSMillis(void)
{
    return (uint32_t)GetTickCount64();
}

void platform_sleep_ms(uint32_t ms)
{
    Sleep((DWORD)ms);
}

int platform_console_prepare_interactive(void)
{
    DWORD input_mode;
    DWORD output_mode;

    if (g_console_prepared) {
        return 0;
    }

    {
        int capture_res = platform_console_capture_modes();
        if (capture_res < 0) {
            return -1;
        }
        if (capture_res > 0 || !g_console_modes_available) {
            g_console_vt_enabled = 0;
            g_console_prepared = 1;
            return 0;
        }
    }

    input_mode = g_console_in_mode;
    input_mode |= ENABLE_EXTENDED_FLAGS;
    input_mode &= ~(DWORD)(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_MOUSE_INPUT);
    input_mode |= ENABLE_INSERT_MODE;
    input_mode &= ~(DWORD)ENABLE_PROCESSED_INPUT;

    if (!SetConsoleMode(g_console_mode_in, input_mode)) {
        platform_console_restore_once();
        errno = EIO;
        return -1;
    }

    output_mode = g_console_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
    if (SetConsoleMode(g_console_mode_out, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        g_console_vt_enabled = 1;
    } else if (SetConsoleMode(g_console_mode_out, output_mode)) {
        g_console_vt_enabled = 0;
    } else {
        platform_console_restore_once();
        errno = EIO;
        return -1;
    }

    if (!g_console_restore_registered) {
        (void)atexit(platform_console_restore_at_exit);
        g_console_restore_registered = 1;
    }

    g_console_prepared = 1;
    return 0;
}

void platform_console_restore_interactive(void)
{
    platform_console_restore_once();
}

int platform_console_supports_vt(void)
{
    return g_console_vt_enabled;
}

int platform_console_poll_input(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;

    while (waited_ms < timeout_ms) {
        if (_kbhit() != 0) {
            return 1;
        }
        Sleep(1U);
        waited_ms++;
    }

    return (_kbhit() != 0) ? 1 : 0;
}

int platform_console_read_char(char *ch)
{
    int c;

    if (ch == NULL) {
        errno = EINVAL;
        return -1;
    }

    c = _getch();
    if (c == EOF) {
        return 0;
    }

    *ch = (char)c;
    return 1;
}

int platform_console_stdin_fd(void)
{
    return _fileno(stdin);
}

int platform_console_stdout_fd(void)
{
    return _fileno(stdout);
}

platform_shell_input_action_t platform_shell_input_classify_last_error(int *err_out)
{
    int err = errno;

    if (err_out != NULL) {
        *err_out = err;
    }

    if (err == EAGAIN || err == ENOENT) {
        return PLATFORM_SHELL_INPUT_ACTION_USER_EXIT;
    }
    if (err == EINTR) {
        return PLATFORM_SHELL_INPUT_ACTION_CONTINUE;
    }
    return PLATFORM_SHELL_INPUT_ACTION_IO_ERROR;
}

void platform_console_flush_stdout(void)
{
    fflush(stdout);
}
