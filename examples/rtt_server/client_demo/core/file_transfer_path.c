#include "file_transfer_path.h"

#include <stdio.h>
#include <string.h>

const char *file_transfer_path_basename(const char *path)
{
    const char *last;
    const char *p;

    if (path == NULL) {
        return NULL;
    }

    last = path;
    for (p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }

    return last;
}

int file_transfer_join_remote_path(const char *remote_cwd,
                                   const char *entry,
                                   char *out,
                                   size_t out_size)
{
    size_t cwd_len;
    int n;

    if (entry == NULL || entry[0] == '\0' || out == NULL || out_size == 0U) {
        return -1;
    }

    if (entry[0] == '/') {
        if (strlen(entry) >= out_size) {
            return -1;
        }
        memcpy(out, entry, strlen(entry) + 1U);
        return 0;
    }

    if (remote_cwd == NULL || remote_cwd[0] == '\0') {
        remote_cwd = "/";
    }

    cwd_len = strlen(remote_cwd);
    while (cwd_len > 1U && remote_cwd[cwd_len - 1U] == '/') {
        --cwd_len;
    }

    if (cwd_len == 1U && remote_cwd[0] == '/') {
        n = snprintf(out, out_size, "/%s", entry);
    } else {
        n = snprintf(out, out_size, "%.*s/%s", (int)cwd_len, remote_cwd, entry);
    }

    if (n < 0 || (size_t)n >= out_size) {
        return -1;
    }

    return 0;
}
