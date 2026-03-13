#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "objc_types.h"

/* ================================================================ */
/* Growable text buffer                                             */
/* ================================================================ */

typedef struct {
    char  *buffer;
    size_t length;
    size_t capacity;
    bool   failed;   /* set if realloc fails; further writes are no-ops */
} FormatBuffer;

FormatBuffer *format_buffer_create(size_t initial_capacity);
void          format_buffer_destroy(FormatBuffer *buf);

void format_append(FormatBuffer *buf, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void format_append_indent(FormatBuffer *buf, int indent, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ================================================================ */
/* Individual element formatters                                    */
/* ================================================================ */

void format_method(FormatBuffer *buf, const ObjCMethod *method, bool is_class_method);
void format_property(FormatBuffer *buf, const ObjCProperty *prop);
void format_ivar(FormatBuffer *buf, const ObjCIvar *ivar);

/* ================================================================ */
/* Top-level formatters                                             */
/* ================================================================ */

void format_class(FormatBuffer *buf, const ObjCClassInfo *cls);
void format_protocol(FormatBuffer *buf, const ObjCProtocolInfo *proto);
void format_category(FormatBuffer *buf, const ObjCCategoryInfo *cat);

/*
 * Format a complete class-dump style output.
 * Returns a heap-allocated string (caller must free), or NULL on failure.
 */
char *format_full_dump(const ObjCMetadata *metadata,
                       const char *binary_name,
                       const char *arch_name,
                       const char *file_type);
