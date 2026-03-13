#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    DIAG_OK = 0,
    DIAG_ERR_INVALID_MAGIC,
    DIAG_ERR_TRUNCATED,
    DIAG_ERR_INVALID_OFFSET,
    DIAG_ERR_UNSUPPORTED_ARCH,
    DIAG_ERR_INVALID_LOAD_COMMAND,
    DIAG_ERR_INVALID_SECTION,
    DIAG_ERR_OBJC_PARSE_FAILED,
    DIAG_ERR_ALLOC_FAILED,
    DIAG_ERR_NO_OBJC_METADATA,
    DIAG_WARN_PARTIAL_METADATA,
    DIAG_WARN_UNRESOLVED_REFERENCE,
    DIAG_WARN_ALIGNMENT,
} DiagCode;

typedef struct {
    DiagCode code;
    char message[256];
    size_t offset; /* file offset where issue occurred */
} DiagEntry;

typedef struct {
    DiagEntry *entries;
    size_t count;
    size_t capacity;
} DiagList;

/// Create a new diagnostics list. Returns NULL on allocation failure.
DiagList *diag_list_create(void);

/// Destroy a diagnostics list and free all memory.
void diag_list_destroy(DiagList *list);

/// Add a diagnostic entry with a static message string.
void diag_add(DiagList *list, DiagCode code, size_t offset, const char *message);

/// Add a diagnostic entry with a formatted message (printf-style).
void diag_add_fmt(DiagList *list, DiagCode code, size_t offset,
                  const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/// Returns true if the list contains any error-level diagnostics.
bool diag_has_errors(const DiagList *list);

/// Returns true if the list contains any warning-level diagnostics.
bool diag_has_warnings(const DiagList *list);

#endif /* DIAGNOSTICS_H */
