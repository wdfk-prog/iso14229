/**
 * @file platform_linux.c
 * @brief Linux implementation for minimal platform abstraction.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

uint32_t platform_tick_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint32_t)(ts.tv_sec * 1000U + ts.tv_nsec / 1000000U);
}

void platform_sleep_ms(uint32_t ms)
{
    struct timespec req;
    struct timespec rem;

    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)((ms % 1000U) * 1000000U);

    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            break;
        }
        req = rem;
    }
}

int platform_console_poll_input(uint32_t timeout_ms)
{
    fd_set readfds;
    struct timeval tv;
    int fd = STDIN_FILENO;
    int ret;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    tv.tv_sec = (time_t)(timeout_ms / 1000U);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000U) * 1000U);

    ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        return ret;
    }

    /* Keep readiness check explicit for parity with original shell logic. */
    if (FD_ISSET(fd, &readfds)) {
        return 1;
    }

    return 0;
}

int platform_console_read_char(char *ch)
{
    ssize_t n;

    if (ch == NULL) {
        errno = EINVAL;
        return -1;
    }

    n = read(STDIN_FILENO, ch, 1);
    if (n > 0) {
        return 1;
    }
    if (n == 0) {
        return 0;
    }

    return -1;
}

void platform_console_flush_stdout(void)
{
    fflush(stdout);
}
