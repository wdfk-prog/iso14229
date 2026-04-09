#include "shell_command_dispatch.h"
#include "cmd_registry.h"
#include "client_config.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *shell_trim_whitespace_inplace(char *line)
{
    char *start;
    char *end;

    if (line == NULL) {
        return NULL;
    }

    start = line;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    if (*start == '\0') {
        line[0] = '\0';
        return line;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';

    if (start != line) {
        memmove(line, start, (size_t)(end - start) + 1U);
    }

    return line;
}

int shell_dispatch_command_line(char *line,
                                shell_local_execute_fn exec_local,
                                shell_remote_send_fn send_remote,
                                int *exit_requested)
{
    char line_copy[CMD_MAX_LINE];
    size_t line_len;
    int local_res;

    if (exit_requested != NULL) {
        *exit_requested = 0;
    }
    if (line == NULL || exec_local == NULL || send_remote == NULL || exit_requested == NULL) {
        return -1;
    }

    (void)shell_trim_whitespace_inplace(line);
    if (line[0] == '\0') {
        return 0;
    }

    if (strcmp(line, "exit") == 0) {
        *exit_requested = 1;
        return 0;
    }

    line_len = strlen(line);
    if (line_len >= sizeof(line_copy)) {
        return send_remote(line);
    }

    memcpy(line_copy, line, line_len + 1U);
    local_res = exec_local(line_copy);

    if (local_res == CMD_EXEC_NOT_FOUND) {
        return send_remote(line);
    }

    return local_res;
}
