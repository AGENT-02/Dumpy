#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define DIAG_INITIAL_CAPACITY 16

DiagList *diag_list_create(void) {
    DiagList *list = (DiagList *)calloc(1, sizeof(DiagList));
    if (!list) return NULL;

    list->entries = (DiagEntry *)calloc(DIAG_INITIAL_CAPACITY, sizeof(DiagEntry));
    if (!list->entries) {
        free(list);
        return NULL;
    }
    list->count = 0;
    list->capacity = DIAG_INITIAL_CAPACITY;
    return list;
}

void diag_list_destroy(DiagList *list) {
    if (!list) return;
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    free(list);
}

static bool diag_ensure_capacity(DiagList *list) {
    if (list->count < list->capacity) return true;

    size_t new_cap = list->capacity * 2;
    if (new_cap < list->capacity) return false; /* overflow */

    DiagEntry *new_entries = (DiagEntry *)realloc(list->entries,
                                                   new_cap * sizeof(DiagEntry));
    if (!new_entries) return false;

    list->entries = new_entries;
    list->capacity = new_cap;
    return true;
}

void diag_add(DiagList *list, DiagCode code, size_t offset, const char *message) {
    if (!list) return;
    if (!diag_ensure_capacity(list)) return;

    DiagEntry *entry = &list->entries[list->count];
    entry->code = code;
    entry->offset = offset;
    if (message) {
        strncpy(entry->message, message, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';
    } else {
        entry->message[0] = '\0';
    }
    list->count++;
}

void diag_add_fmt(DiagList *list, DiagCode code, size_t offset,
                  const char *fmt, ...) {
    if (!list || !fmt) return;
    if (!diag_ensure_capacity(list)) return;

    DiagEntry *entry = &list->entries[list->count];
    entry->code = code;
    entry->offset = offset;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    va_end(args);

    list->count++;
}

static bool is_error_code(DiagCode code) {
    switch (code) {
        case DIAG_ERR_INVALID_MAGIC:
        case DIAG_ERR_TRUNCATED:
        case DIAG_ERR_INVALID_OFFSET:
        case DIAG_ERR_UNSUPPORTED_ARCH:
        case DIAG_ERR_INVALID_LOAD_COMMAND:
        case DIAG_ERR_INVALID_SECTION:
        case DIAG_ERR_OBJC_PARSE_FAILED:
        case DIAG_ERR_ALLOC_FAILED:
        case DIAG_ERR_NO_OBJC_METADATA:
            return true;
        default:
            return false;
    }
}

static bool is_warning_code(DiagCode code) {
    switch (code) {
        case DIAG_WARN_PARTIAL_METADATA:
        case DIAG_WARN_UNRESOLVED_REFERENCE:
            return true;
        default:
            return false;
    }
}

bool diag_has_errors(const DiagList *list) {
    if (!list) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (is_error_code(list->entries[i].code)) return true;
    }
    return false;
}

bool diag_has_warnings(const DiagList *list) {
    if (!list) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (is_warning_code(list->entries[i].code)) return true;
    }
    return false;
}
