#include "local_path_completion.h"

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *find_last_path_separator(const char *path)
{
    const char *last_fwd;
    const char *last_back;

    if (path == NULL) {
        return NULL;
    }

    last_fwd = strrchr(path, '/');
    last_back = strrchr(path, '\\');

    if (last_fwd == NULL) {
        return last_back;
    }
    if (last_back == NULL) {
        return last_fwd;
    }
    return (last_fwd > last_back) ? last_fwd : last_back;
}

static int path_has_trailing_separator(const char *path)
{
    size_t len;

    if (path == NULL) {
        return 0;
    }

    len = strlen(path);
    if (len == 0U) {
        return 0;
    }

    return (path[len - 1U] == '/' || path[len - 1U] == '\\');
}

static void join_path(char *dst,
                      size_t dst_size,
                      const char *dir,
                      const char *name,
                      char default_sep)
{
    int needs_sep = 0;

    if (dst == NULL || dst_size == 0U) {
        return;
    }

    dst[0] = '\0';

    if (dir == NULL || dir[0] == '\0') {
        (void)snprintf(dst, dst_size, "%s", name != NULL ? name : "");
        return;
    }

    needs_sep = !path_has_trailing_separator(dir);
    if (needs_sep) {
        (void)snprintf(dst,
                       dst_size,
                       "%s%c%s",
                       dir,
                       default_sep,
                       name != NULL ? name : "");
    } else {
        (void)snprintf(dst,
                       dst_size,
                       "%s%s",
                       dir,
                       name != NULL ? name : "");
    }
}

void local_path_completion_collect(const char *line,
                                   size_t prefix_len,
                                   const char *word_part,
                                   char default_sep,
                                   local_path_completion_emit_fn emit,
                                   void *ctx)
{
    char dir_part[512] = {0};
    char search_dir[512] = ".";
    char entry_prefix[256] = {0};
    const char *last_sep;
    char used_sep;
    DIR *dir;
    struct dirent *entry;

    if (line == NULL || word_part == NULL || emit == NULL) {
        return;
    }

    used_sep = (default_sep == '\\') ? '\\' : '/';
    last_sep = find_last_path_separator(word_part);
    if (last_sep != NULL) {
        size_t dir_len = (size_t)(last_sep - word_part + 1);

        used_sep = *last_sep;
        if (dir_len >= sizeof(dir_part)) {
            return;
        }

        memcpy(dir_part, word_part, dir_len);
        dir_part[dir_len] = '\0';
        (void)snprintf(entry_prefix, sizeof(entry_prefix), "%s", last_sep + 1);
        (void)snprintf(search_dir, sizeof(search_dir), "%s", dir_part);
    } else {
        (void)snprintf(entry_prefix, sizeof(entry_prefix), "%s", word_part);
    }

    dir = opendir(search_dir);
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char candidate[1024];
        char full_path[1024];
        char full_completion[1024];
        struct stat st;
        size_t candidate_len;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (strncmp(entry->d_name, entry_prefix, strlen(entry_prefix)) != 0) {
            continue;
        }

        if (dir_part[0] != '\0') {
            (void)snprintf(candidate, sizeof(candidate), "%s%s", dir_part, entry->d_name);
        } else {
            (void)snprintf(candidate, sizeof(candidate), "%s", entry->d_name);
        }

        join_path(full_path, sizeof(full_path), search_dir, entry->d_name, used_sep);
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            candidate_len = strlen(candidate);
            if (candidate_len + 1U < sizeof(candidate)) {
                candidate[candidate_len] = used_sep;
                candidate[candidate_len + 1U] = '\0';
            }
        }

        if (prefix_len + strlen(candidate) >= sizeof(full_completion)) {
            continue;
        }

        memcpy(full_completion, line, prefix_len);
        strcpy(full_completion + prefix_len, candidate);
        if (emit(ctx, full_completion) != 0) {
            break;
        }
    }

    (void)closedir(dir);
}
