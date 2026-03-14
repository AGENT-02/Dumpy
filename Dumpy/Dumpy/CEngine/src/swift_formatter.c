#include "swift_formatter.h"
#include "objc_formatter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================ */
/* Basic Swift stdlib type demangling                               */
/* ================================================================ */

/*
 * Demangle a simple Swift mangled type name.
 * Handles common Swift stdlib abbreviations.
 * Returns a heap-allocated string, or NULL on failure.
 */
static const char *demangle_simple(const char *mangled) {
    if (!mangled || mangled[0] == '\0') return NULL;

    /* Common Swift stdlib abbreviations */
    if (strcmp(mangled, "Si") == 0) return "Int";
    if (strcmp(mangled, "Sd") == 0) return "Double";
    if (strcmp(mangled, "Sf") == 0) return "Float";
    if (strcmp(mangled, "Sb") == 0) return "Bool";
    if (strcmp(mangled, "SS") == 0) return "String";
    if (strcmp(mangled, "SQ") == 0) return "Optional";
    if (strcmp(mangled, "Sq") == 0) return "Optional";
    if (strcmp(mangled, "Sa") == 0) return "Array";
    if (strcmp(mangled, "SD") == 0) return "Dictionary";
    if (strcmp(mangled, "Su") == 0) return "UInt";
    if (strcmp(mangled, "Ss") == 0) return "Substring";
    if (strcmp(mangled, "SV") == 0) return "UnsafeRawPointer";
    if (strcmp(mangled, "Sv") == 0) return "UnsafeMutableRawPointer";

    return NULL;
}

/*
 * Get a display name for a mangled type.
 * Returns a heap-allocated string (caller must free).
 */
static char *get_display_type(const char *mangled) {
    if (!mangled || mangled[0] == '\0') return strdup("Any");

    const char *simple = demangle_simple(mangled);
    if (simple) return strdup(simple);

    /* Return the mangled name as-is if we can't demangle it */
    return strdup(mangled);
}

/*
 * Return the Swift keyword for a type kind.
 */
static const char *kind_keyword(uint32_t kind) {
    switch (kind) {
        case SWIFT_KIND_CLASS:  return "class";
        case SWIFT_KIND_STRUCT: return "struct";
        case SWIFT_KIND_ENUM:   return "enum";
        default:                return "/* unknown */";
    }
}

/* ================================================================ */
/* Top-level formatter                                              */
/* ================================================================ */

char *format_swift_dump(const SwiftMetadata *metadata,
                        const char *binary_name,
                        const char *arch_name)
{
    if (!metadata || !metadata->has_swift_metadata || metadata->type_count == 0)
        return NULL;

    FormatBuffer *buf = format_buffer_create(8192);
    if (!buf) return NULL;

    /* Header */
    format_append(buf, "// Swift type metadata");
    if (binary_name) format_append(buf, " from %s", binary_name);
    if (arch_name)   format_append(buf, " (%s)", arch_name);
    format_append(buf, "\n");
    format_append(buf, "// %zu type(s) found\n\n", metadata->type_count);

    for (size_t i = 0; i < metadata->type_count; i++) {
        const SwiftTypeInfo *type = &metadata->types[i];
        if (!type->name) continue;

        /* Type declaration line */
        format_append(buf, "%s %s", kind_keyword(type->kind), type->name);

        if (type->superclass_name && type->superclass_name[0] != '\0') {
            /* Try to demangle superclass name */
            const char *simple = demangle_simple(type->superclass_name);
            if (simple) {
                format_append(buf, ": %s", simple);
            } else {
                format_append(buf, ": %s", type->superclass_name);
            }
        }

        format_append(buf, " {\n");

        /* Fields */
        for (size_t f = 0; f < type->field_count; f++) {
            const SwiftFieldInfo *field = &type->fields[f];
            if (!field->name) continue;

            if (type->kind == SWIFT_KIND_ENUM) {
                /* Enum cases */
                format_append(buf, "    case %s", field->name);
                if (field->mangled_type_name &&
                    field->mangled_type_name[0] != '\0') {
                    char *display = get_display_type(field->mangled_type_name);
                    if (display) {
                        format_append(buf, "(%s)", display);
                        free(display);
                    }
                }
                format_append(buf, "\n");
            } else {
                /* Struct/class fields */
                const char *var_let = field->is_var ? "var" : "let";
                char *display = get_display_type(field->mangled_type_name);
                format_append(buf, "    %s %s: %s\n",
                              var_let, field->name,
                              display ? display : "Any");
                free(display);
            }
        }

        format_append(buf, "}\n");

        /* Conformances comment */
        if (type->conformance_count > 0 && type->conformances) {
            format_append(buf, "// conforms to:");
            for (size_t c = 0; c < type->conformance_count; c++) {
                if (type->conformances[c]) {
                    format_append(buf, "%s%s",
                                  (c > 0) ? ", " : " ",
                                  type->conformances[c]);
                }
            }
            format_append(buf, "\n");
        }

        format_append(buf, "\n");
    }

    if (buf->failed) {
        format_buffer_destroy(buf);
        return NULL;
    }

    /* Extract the string and destroy the buffer */
    char *result = buf->buffer;
    buf->buffer = NULL;
    format_buffer_destroy(buf);
    return result;
}
