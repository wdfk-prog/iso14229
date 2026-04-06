/**
 * @file platform_windows.c
 * @brief Windows / MSYS2 MINGW64 implementation for minimal platform abstraction.
 * @details Task 6B only needs a buildable shell/platform skeleton. Runtime UX and
 *          terminal parity are deferred to Task 8.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <io.h>

#include "platform.h"

#include <errno.h>
#include <stdio.h>

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

int platform_console_poll_input(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0;

    while (waited_ms < timeout_ms) {
        if (_kbhit() != 0) {
            return 1;
        }
        Sleep(1);
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
