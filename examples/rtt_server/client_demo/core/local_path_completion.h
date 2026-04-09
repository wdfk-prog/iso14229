#ifndef LOCAL_PATH_COMPLETION_H
#define LOCAL_PATH_COMPLETION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*local_path_completion_emit_fn)(void *ctx, const char *completion);

/**
 * @brief Expand a local filesystem token into shell completions.
 * @details Scans the local filesystem relative to the token in @p word_part,
 *          preserves any typed directory prefix, appends a directory separator
 *          to directory matches, and emits full-line completions prefixed by
 *          the first @p prefix_len bytes of @p line.
 *
 * @param line Full interactive input line.
 * @param prefix_len Number of prefix bytes to preserve from @p line.
 * @param word_part Active token to complete.
 * @param default_sep Directory separator appended for directory matches when
 *        @p word_part does not already imply one. Pass '/' on POSIX and '\\'
 *        on Windows-style shells.
 * @param emit Callback invoked for each generated completion.
 * @param ctx Opaque callback context.
 */
void local_path_completion_collect(const char *line,
                                   size_t prefix_len,
                                   const char *word_part,
                                   char default_sep,
                                   local_path_completion_emit_fn emit,
                                   void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LOCAL_PATH_COMPLETION_H */
