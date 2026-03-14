#include "swift_parser.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================ */
/* Named constants for validation limits                            */
/* ================================================================ */

#define SWIFT_MAX_TYPES          50000
#define SWIFT_MAX_FIELDS         10000
#define SWIFT_MAX_STRING_LEN     4096
#define SWIFT_KIND_MASK          0x1F   /* bits 0-4 of flags */

/* ================================================================ */
/* Cleanup functions                                                */
/* ================================================================ */

void swift_field_info_destroy(SwiftFieldInfo *fi) {
    if (!fi) return;
    free(fi->name);
    free(fi->mangled_type_name);
    fi->name = NULL;
    fi->mangled_type_name = NULL;
}

void swift_type_info_destroy(SwiftTypeInfo *ti) {
    if (!ti) return;
    free(ti->name);
    free(ti->superclass_name);
    ti->name = NULL;
    ti->superclass_name = NULL;

    if (ti->fields) {
        for (size_t i = 0; i < ti->field_count; i++) {
            swift_field_info_destroy(&ti->fields[i]);
        }
        free(ti->fields);
        ti->fields = NULL;
    }
    ti->field_count = 0;

    if (ti->conformances) {
        for (size_t i = 0; i < ti->conformance_count; i++) {
            free(ti->conformances[i]);
        }
        free(ti->conformances);
        ti->conformances = NULL;
    }
    ti->conformance_count = 0;
}

void swift_metadata_destroy(SwiftMetadata *meta) {
    if (!meta) return;
    if (meta->types) {
        for (size_t i = 0; i < meta->type_count; i++) {
            swift_type_info_destroy(&meta->types[i]);
        }
        free(meta->types);
        meta->types = NULL;
    }
    meta->type_count = 0;
    meta->has_swift_metadata = false;
}

/* ================================================================ */
/* Internal helpers                                                 */
/* ================================================================ */

/*
 * Resolve a relative pointer stored as int32_t at `offset` in the file.
 * The resolved file offset = offset + (int32_t value at offset).
 * Returns false if the read fails or the result is out of bounds.
 */
static bool resolve_relative_pointer(const uint8_t *data, size_t data_size,
                                     size_t offset, size_t *resolved)
{
    int32_t rel;
    if (!safe_read_bytes(data, data_size, offset, &rel, sizeof(int32_t))) {
        return false;
    }

    /* Compute resolved offset: position of the pointer + relative value */
    int64_t result = (int64_t)offset + (int64_t)rel;
    if (result < 0 || (size_t)result >= data_size) {
        return false;
    }

    *resolved = (size_t)result;
    return true;
}

/*
 * Read a null-terminated string at a file offset.
 * Returns a heap-allocated copy, or NULL on failure.
 */
static char *read_string_at_offset(const uint8_t *data, size_t data_size,
                                   size_t offset)
{
    const char *str = safe_read_string(data, data_size, offset,
                                       SWIFT_MAX_STRING_LEN);
    if (!str) return NULL;
    return strdup(str);
}

/*
 * Parse a field descriptor at the given file offset.
 * The field descriptor layout:
 *   int32_t mangled_type_name  (relative pointer)
 *   int32_t superclass         (relative pointer)
 *   uint16_t kind
 *   uint16_t field_record_size
 *   uint32_t num_fields
 *   followed by num_fields field records
 *
 * Field record layout:
 *   uint32_t flags
 *   int32_t mangled_type_name  (relative pointer)
 *   int32_t field_name         (relative pointer)
 */
static bool parse_field_descriptor(const uint8_t *data, size_t data_size,
                                   size_t fd_offset,
                                   SwiftTypeInfo *type_info,
                                   DiagList *diags)
{
    /* Read superclass relative pointer (at fd_offset + 4) */
    size_t superclass_str_offset;
    if (resolve_relative_pointer(data, data_size, fd_offset + 4,
                                 &superclass_str_offset)) {
        char *super_name = read_string_at_offset(data, data_size,
                                                  superclass_str_offset);
        if (super_name && super_name[0] != '\0') {
            type_info->superclass_name = super_name;
        } else {
            free(super_name);
        }
    }

    /* Read kind (uint16_t at fd_offset + 8) and field_record_size (uint16_t at fd_offset + 10) */
    uint16_t fd_kind = 0;
    uint16_t field_record_size = 0;
    if (!safe_read_bytes(data, data_size, fd_offset + 8, &fd_kind, sizeof(uint16_t)) ||
        !safe_read_bytes(data, data_size, fd_offset + 10, &field_record_size, sizeof(uint16_t))) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, fd_offset,
                                "Failed to read field descriptor header at 0x%zX", fd_offset);
        return false;
    }

    /* Read num_fields (uint32_t at fd_offset + 12) */
    uint32_t num_fields = 0;
    if (!safe_read_bytes(data, data_size, fd_offset + 12, &num_fields, sizeof(uint32_t))) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, fd_offset,
                                "Failed to read num_fields at 0x%zX", fd_offset + 12);
        return false;
    }

    if (num_fields == 0) return true;

    if (num_fields > SWIFT_MAX_FIELDS) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, fd_offset,
                                "Field count %u truncated to %d", num_fields, SWIFT_MAX_FIELDS);
        num_fields = SWIFT_MAX_FIELDS;
    }

    /* Validate field_record_size: each record is at least 12 bytes (flags + 2 relative ptrs) */
    if (field_record_size < 12) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, fd_offset,
                                "Invalid field_record_size %u at 0x%zX",
                                field_record_size, fd_offset);
        return false;
    }

    type_info->fields = (SwiftFieldInfo *)calloc(num_fields, sizeof(SwiftFieldInfo));
    if (!type_info->fields) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate Swift fields array");
        return false;
    }

    /* Field records start at fd_offset + 16 */
    size_t records_start = fd_offset + 16;
    size_t parsed_count = 0;

    for (uint32_t f = 0; f < num_fields; f++) {
        size_t rec_offset = records_start + (size_t)f * (size_t)field_record_size;

        /* Read flags (uint32_t at rec_offset) */
        uint32_t flags = 0;
        if (!safe_read_bytes(data, data_size, rec_offset, &flags, sizeof(uint32_t))) {
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, rec_offset,
                                    "Failed to read field record flags at 0x%zX", rec_offset);
            break;
        }

        SwiftFieldInfo *field = &type_info->fields[parsed_count];

        /* Bit 1 of flags indicates var (1) vs let (0) */
        field->is_var = (flags & 0x2) != 0;

        /* Read mangled_type_name (relative pointer at rec_offset + 4) */
        size_t type_name_offset;
        if (resolve_relative_pointer(data, data_size, rec_offset + 4,
                                     &type_name_offset)) {
            field->mangled_type_name = read_string_at_offset(data, data_size,
                                                              type_name_offset);
        }

        /* Read field_name (relative pointer at rec_offset + 8) */
        size_t field_name_offset;
        if (resolve_relative_pointer(data, data_size, rec_offset + 8,
                                     &field_name_offset)) {
            field->name = read_string_at_offset(data, data_size,
                                                 field_name_offset);
        }

        /* Only count fields where we got at least a name */
        if (field->name) {
            parsed_count++;
        } else {
            /* Clean up partial field */
            free(field->mangled_type_name);
            field->mangled_type_name = NULL;
        }
    }

    type_info->field_count = parsed_count;
    return true;
}

/* ================================================================ */
/* Main parser                                                      */
/* ================================================================ */

DiagCode swift_parse_metadata(const MachOContext *ctx,
                              const SectionsInfo *sections,
                              const VMMap *vmmap,
                              SwiftMetadata *result,
                              DiagList *diags)
{
    if (!ctx || !sections || !result) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to swift_parse_metadata");
        return DIAG_ERR_TRUNCATED;
    }

    memset(result, 0, sizeof(SwiftMetadata));

    /* Find the __swift5_types section */
    const SectionInfo *types_sect = sections->swift5_types;
    if (!types_sect) {
        types_sect = find_section(sections, "__TEXT", "__swift5_types");
    }

    if (!types_sect || types_sect->size == 0) {
        /* No Swift metadata present */
        result->has_swift_metadata = false;
        return DIAG_OK;
    }

    result->has_swift_metadata = true;

    /* __swift5_types contains an array of int32_t relative pointers.
     * Each relative pointer resolves to a type descriptor. */
    size_t entry_count = (size_t)(types_sect->size / sizeof(int32_t));
    if (entry_count == 0) return DIAG_OK;

    if (entry_count > SWIFT_MAX_TYPES) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA,
                                types_sect->offset,
                                "Swift type count %zu truncated to %d",
                                entry_count, SWIFT_MAX_TYPES);
        entry_count = SWIFT_MAX_TYPES;
    }

    result->types = (SwiftTypeInfo *)calloc(entry_count, sizeof(SwiftTypeInfo));
    if (!result->types) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate Swift types array");
        return DIAG_ERR_ALLOC_FAILED;
    }

    size_t parsed_count = 0;
    size_t sect_file_offset = types_sect->offset;

    for (size_t i = 0; i < entry_count; i++) {
        size_t ptr_offset = sect_file_offset + i * sizeof(int32_t);

        /* Resolve the relative pointer to get the type descriptor offset */
        size_t desc_offset;
        if (!resolve_relative_pointer(ctx->data, ctx->size, ptr_offset,
                                      &desc_offset)) {
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA,
                                    ptr_offset,
                                    "Failed to resolve Swift type pointer at 0x%zX",
                                    ptr_offset);
            continue;
        }

        /*
         * Type descriptor layout:
         *   int32_t flags                  (offset +0)
         *   int32_t parent                 (offset +4, relative pointer)
         *   int32_t name                   (offset +8, relative pointer to string)
         *   int32_t access_function        (offset +12, relative pointer)
         *   int32_t field_descriptor       (offset +16, relative pointer)
         */

        /* Read flags */
        int32_t raw_flags;
        if (!safe_read_bytes(ctx->data, ctx->size, desc_offset,
                             &raw_flags, sizeof(int32_t))) {
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA,
                                    desc_offset,
                                    "Failed to read type descriptor flags at 0x%zX",
                                    desc_offset);
            continue;
        }

        uint32_t kind = (uint32_t)raw_flags & SWIFT_KIND_MASK;

        /* We only handle class, struct, enum */
        if (kind != SWIFT_KIND_CLASS && kind != SWIFT_KIND_STRUCT &&
            kind != SWIFT_KIND_ENUM) {
            continue;
        }

        SwiftTypeInfo *type_info = &result->types[parsed_count];
        type_info->kind = kind;

        /* Read name (relative pointer at desc_offset + 8) */
        size_t name_offset;
        if (resolve_relative_pointer(ctx->data, ctx->size, desc_offset + 8,
                                     &name_offset)) {
            type_info->name = read_string_at_offset(ctx->data, ctx->size,
                                                     name_offset);
        }

        if (!type_info->name) {
            /* Skip types without a name */
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA,
                                    desc_offset,
                                    "Failed to read Swift type name at 0x%zX",
                                    desc_offset);
            continue;
        }

        /* Read field_descriptor (relative pointer at desc_offset + 16) */
        size_t fd_offset;
        if (resolve_relative_pointer(ctx->data, ctx->size, desc_offset + 16,
                                     &fd_offset)) {
            parse_field_descriptor(ctx->data, ctx->size, fd_offset,
                                   type_info, diags);
        }

        parsed_count++;
    }

    result->type_count = parsed_count;

    if (parsed_count == 0 && entry_count > 0) {
        if (diags) diag_add(diags, DIAG_WARN_PARTIAL_METADATA, sect_file_offset,
                            "No Swift types could be parsed from __swift5_types");
    }

    return DIAG_OK;
}
