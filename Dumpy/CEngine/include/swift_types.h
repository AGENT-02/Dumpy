#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ================================================================ */
/* Swift type metadata kind constants                               */
/* ================================================================ */

#define SWIFT_KIND_CLASS   16
#define SWIFT_KIND_STRUCT  17
#define SWIFT_KIND_ENUM    18

/* ================================================================ */
/* Parsed Swift metadata structures                                 */
/* ================================================================ */

typedef struct {
    char *name;               /* field name */
    char *mangled_type_name;  /* mangled type name (may be NULL) */
    bool  is_var;             /* true = var, false = let */
} SwiftFieldInfo;

typedef struct {
    char  *name;              /* type name */
    uint32_t kind;            /* SWIFT_KIND_CLASS / STRUCT / ENUM */
    char  *superclass_name;   /* superclass name (may be NULL) */
    SwiftFieldInfo *fields;   /* array of fields */
    size_t field_count;
    char **conformances;      /* array of protocol conformance names */
    size_t conformance_count;
} SwiftTypeInfo;

typedef struct {
    SwiftTypeInfo *types;
    size_t type_count;
    bool   has_swift_metadata;
} SwiftMetadata;

/* ================================================================ */
/* Cleanup functions                                                */
/* ================================================================ */

void swift_field_info_destroy(SwiftFieldInfo *fi);
void swift_type_info_destroy(SwiftTypeInfo *ti);
void swift_metadata_destroy(SwiftMetadata *meta);
