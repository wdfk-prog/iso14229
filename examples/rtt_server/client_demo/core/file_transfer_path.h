#ifndef FILE_TRANSFER_PATH_H
#define FILE_TRANSFER_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *file_transfer_path_basename(const char *path);
int file_transfer_join_remote_path(const char *remote_cwd,
                                   const char *entry,
                                   char *out,
                                   size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
