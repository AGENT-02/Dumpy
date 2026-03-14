#include "objc_resolver.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ---------------------------------------------------------------- */
/* Pointer tag stripping                                            */
/* ---------------------------------------------------------------- */

/*
 * ARM64 virtual addresses are 48 bits. Top 16 bits may contain PAC tags
 * or chained fixup metadata.
 *
 * This is the canonical, single implementation for PAC/tag stripping.
 * strip_pac_pointer() in macho_vmmap.c should delegate here.
 */
uint64_t strip_pointer_tags(uint64_t ptr, bool is_64bit)
{
    if (!is_64bit) {
        return ptr;
    }

    /*
     * Check if this looks like a chained fixup pointer.
     * Chained fixup rebase pointers have bit 62 set in the on-disk
     * representation.  In that case, the target is in bits 0..50.
     */
    if (ptr & ((uint64_t)1 << 62)) {
        return ptr & 0x0007FFFFFFFFFFFFULL; /* bits 0-50 */
    }

    /*
     * Standard PAC pointer: mask to 48-bit canonical arm64 address.
     * ARM64 virtual addresses are 48 bits. Top 16 bits may contain
     * PAC tags or chained fixup metadata.
     */
    return ptr & 0x0000FFFFFFFFFFFFULL;
}

/* ---------------------------------------------------------------- */
/* String pointer resolution                                       */
/* ---------------------------------------------------------------- */

/* Maximum string length returned by resolve_string_pointer */
#define MAX_RESOLVED_STRING_LENGTH 8192

/*
 * Returns a heap-allocated (strdup'd) copy of the string at the given
 * VM address, or NULL on failure.  Callers MUST free() the returned
 * pointer.  The function never returns a direct pointer into the
 * mapped binary -- it always copies via strdup() or malloc().
 */
const char *resolve_string_pointer(const MachOContext *ctx, const VMMap *vmmap,
                                   uint64_t ptr, DiagList *diags)
{
    if (ptr == 0) {
        return NULL;
    }

    uint64_t clean = strip_pointer_tags(ptr, ctx->is_64bit);

    const char *s = vmmap_read_string(vmmap, ctx->data, ctx->size, clean, 4096);
    if (!s || s[0] == '\0') {
        /* Try without stripping, in case it was already clean */
        if (clean != ptr) {
            s = vmmap_read_string(vmmap, ctx->data, ctx->size, ptr, 4096);
        }
        if (!s) {
            return NULL;
        }
    }

    /* Validate string length; truncate if it exceeds the maximum */
    size_t len = strlen(s);
    if (len > MAX_RESOLVED_STRING_LENGTH) {
        char *truncated = malloc(MAX_RESOLVED_STRING_LENGTH + 4);
        if (!truncated) return NULL;
        memcpy(truncated, s, MAX_RESOLVED_STRING_LENGTH);
        truncated[MAX_RESOLVED_STRING_LENGTH]     = '.';
        truncated[MAX_RESOLVED_STRING_LENGTH + 1] = '.';
        truncated[MAX_RESOLVED_STRING_LENGTH + 2] = '.';
        truncated[MAX_RESOLVED_STRING_LENGTH + 3] = '\0';
        return truncated;
    }

    return strdup(s);
}

/* ---------------------------------------------------------------- */
/* Class name resolution                                            */
/* ---------------------------------------------------------------- */

const char *resolve_class_name(const MachOContext *ctx, const VMMap *vmmap,
                               const SectionsInfo *sections, uint64_t class_ptr,
                               DiagList *diags)
{
    if (class_ptr == 0) {
        return NULL;
    }

    uint64_t clean_ptr = strip_pointer_tags(class_ptr, ctx->is_64bit);

    /*
     * Read the objc_class structure at clean_ptr, then follow .data
     * to class_ro_t, then read class_ro_t.name.
     */

    if (ctx->is_64bit) {
        /* Read the data field (5th pointer, offset = 4 * 8 = 32) */
        uint64_t data_ptr = 0;
        if (!vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                                clean_ptr + 32, true, &data_ptr)) {
            return NULL;
        }
        data_ptr = data_ptr & OBJC_CLASS_DATA_MASK;
        data_ptr = strip_pointer_tags(data_ptr, true);

        /*
         * class_ro_t 64-bit: name is at offset
         *   flags(4) + instance_start(4) + instance_size(4) + pad(4)
         *   + ivar_layout(8) + name(8)
         * = offset 24
         */
        uint64_t name_ptr = 0;
        if (!vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                                data_ptr + 24, true, &name_ptr)) {
            return NULL;
        }

        return resolve_string_pointer(ctx, vmmap, name_ptr, diags);
    } else {
        /* 32-bit: data field at offset 4 * 4 = 16 */
        uint64_t data_ptr = 0;
        if (!vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                                clean_ptr + 16, false, &data_ptr)) {
            return NULL;
        }
        data_ptr = data_ptr & OBJC_CLASS_DATA_MASK;

        /*
         * class_ro_t 32-bit: name at offset
         *   flags(4) + instance_start(4) + instance_size(4)
         *   + ivar_layout(4) + name(4)
         * = offset 16
         */
        uint64_t name_ptr = 0;
        if (!vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                                data_ptr + 16, false, &name_ptr)) {
            return NULL;
        }

        return resolve_string_pointer(ctx, vmmap, name_ptr, diags);
    }
}

/* ---------------------------------------------------------------- */
/* Type encoding decoder                                            */
/* ---------------------------------------------------------------- */

/* Maximum recursion depth for type decoding */
#define MAX_TYPE_DEPTH 32

/*
 * Skip past any numeric characters (stack offsets in method type encodings).
 */
static const char *skip_digits(const char *p)
{
    while (*p && isdigit((unsigned char)*p)) {
        p++;
    }
    return p;
}

/*
 * Decode a single type from the encoding string, advancing *cursor.
 * Returns a heap-allocated string.
 * depth tracks recursion to prevent stack overflow on malformed input.
 */
static char *decode_single_type(const char **cursor, int depth)
{
    if (depth > MAX_TYPE_DEPTH) {
        return strdup("/* nested type too deep */");
    }

    if (!cursor || !*cursor || !**cursor) {
        return strdup("void");
    }

    const char *p = *cursor;

    /* Skip const qualifier */
    bool is_const = false;
    if (*p == 'r') {
        is_const = true;
        p++;
    }

    char *result = NULL;

    switch (*p) {
    case 'c': p++; result = strdup("char"); break;
    case 'i': p++; result = strdup("int"); break;
    case 's': p++; result = strdup("short"); break;
    case 'l': p++; result = strdup("long"); break;
    case 'q': p++; result = strdup("long long"); break;
    case 'C': p++; result = strdup("unsigned char"); break;
    case 'I': p++; result = strdup("unsigned int"); break;
    case 'S': p++; result = strdup("unsigned short"); break;
    case 'L': p++; result = strdup("unsigned long"); break;
    case 'Q': p++; result = strdup("unsigned long long"); break;
    case 'f': p++; result = strdup("float"); break;
    case 'd': p++; result = strdup("double"); break;
    case 'B': p++; result = strdup("BOOL"); break;
    case 'v': p++; result = strdup("void"); break;
    case '*': p++; result = strdup("char *"); break;
    case '#': p++; result = strdup("Class"); break;
    case ':': p++; result = strdup("SEL"); break;
    case '?': p++; result = strdup("/*function pointer*/"); break;

    case '@': {
        p++;
        if (*p == '"') {
            /* @"ClassName" */
            p++;
            const char *end = strchr(p, '"');
            if (end) {
                size_t len = (size_t)(end - p);
                /* Allocate for "ClassName *\0" */
                result = malloc(len + 3);
                if (result) {
                    memcpy(result, p, len);
                    result[len] = ' ';
                    result[len + 1] = '*';
                    result[len + 2] = '\0';
                }
                p = end + 1;
            } else {
                result = strdup("id");
            }
        } else if (*p == '?') {
            /* @? = block */
            p++;
            result = strdup("id /* block */");
        } else {
            result = strdup("id");
        }
        break;
    }

    case '^': {
        p++;
        char *inner = decode_single_type(&p, depth + 1);
        if (inner) {
            size_t len = strlen(inner);
            result = malloc(len + 3);
            if (result) {
                snprintf(result, len + 3, "%s *", inner);
            }
            free(inner);
        } else {
            result = strdup("void *");
        }
        break;
    }

    case '{': {
        /* {StructName=...} or {StructName} */
        p++;
        const char *eq = strchr(p, '=');
        const char *end = strchr(p, '}');
        if (end) {
            const char *name_end = eq && eq < end ? eq : end;
            size_t nlen = (size_t)(name_end - p);
            if (nlen > 0 && nlen < 256) {
                result = malloc(nlen + 8);
                if (result) {
                    snprintf(result, nlen + 8, "struct %.*s", (int)nlen, p);
                }
            } else {
                result = strdup("struct ?");
            }
            /* Skip to closing brace, handling nested braces */
            int brace_depth = 1;
            const char *scan = p;
            while (*scan && brace_depth > 0) {
                if (*scan == '{') brace_depth++;
                if (*scan == '}') brace_depth--;
                scan++;
            }
            p = scan;
        } else {
            /* Unterminated struct: no closing '}' found */
            const char *name_start = p;
            while (*p && *p != '=' && *p != '}') p++;
            size_t nlen = (size_t)(p - name_start);
            if (nlen > 0 && nlen < 256) {
                size_t buflen = nlen + 30;
                result = malloc(buflen);
                if (result) {
                    snprintf(result, buflen, "struct %.*s /* unterminated */",
                             (int)nlen, name_start);
                }
            } else {
                result = strdup("struct ? /* unterminated */");
            }
            /* Advance past whatever remains */
            while (*p) p++;
        }
        break;
    }

    case '(': {
        /* (UnionName=...) */
        p++;
        const char *eq = strchr(p, '=');
        const char *end = strchr(p, ')');
        if (end) {
            const char *name_end = eq && eq < end ? eq : end;
            size_t nlen = (size_t)(name_end - p);
            if (nlen > 0 && nlen < 256) {
                result = malloc(nlen + 7);
                if (result) {
                    snprintf(result, nlen + 7, "union %.*s", (int)nlen, p);
                }
            } else {
                result = strdup("union ?");
            }
            int paren_depth = 1;
            const char *scan = p;
            while (*scan && paren_depth > 0) {
                if (*scan == '(') paren_depth++;
                if (*scan == ')') paren_depth--;
                scan++;
            }
            p = scan;
        } else {
            /* Unterminated union: no closing ')' found */
            const char *name_start = p;
            while (*p && *p != '=' && *p != ')') p++;
            size_t nlen = (size_t)(p - name_start);
            if (nlen > 0 && nlen < 256) {
                size_t buflen = nlen + 28;
                result = malloc(buflen);
                if (result) {
                    snprintf(result, buflen, "union %.*s /* unterminated */",
                             (int)nlen, name_start);
                }
            } else {
                result = strdup("union ? /* unterminated */");
            }
            while (*p) p++;
        }
        break;
    }

    case '[': {
        /* [12i] = int[12] */
        p++;
        /* Parse count */
        const char *num_start = p;
        while (*p && isdigit((unsigned char)*p)) p++;
        size_t count_len = (size_t)(p - num_start);
        char count_buf[32];
        if (count_len > 0 && count_len < sizeof(count_buf)) {
            memcpy(count_buf, num_start, count_len);
            count_buf[count_len] = '\0';
        } else {
            count_buf[0] = '0';
            count_buf[1] = '\0';
        }

        char *elem = decode_single_type(&p, depth + 1);

        /* Check for closing bracket; handle unterminated */
        if (*p == ']') {
            p++;
        } else {
            /* Unterminated array */
            if (elem) {
                size_t elen = strlen(elem);
                size_t rlen = elen + count_len + 24;
                result = malloc(rlen);
                if (result) {
                    snprintf(result, rlen, "%s[%s] /* unterminated */",
                             elem, count_buf);
                }
                free(elem);
                elem = NULL;
            } else {
                result = strdup("void * /* unterminated */");
            }
        }

        if (elem) {
            size_t rlen = strlen(elem) + count_len + 4;
            result = malloc(rlen);
            if (result) {
                snprintf(result, rlen, "%s[%s]", elem, count_buf);
            }
            free(elem);
        } else if (!result) {
            result = strdup("void *");
        }
        break;
    }

    case 'b': {
        /* bN = bitfield of N bits */
        p++;
        const char *start = p;
        while (*p && isdigit((unsigned char)*p)) p++;
        size_t nlen = (size_t)(p - start);
        if (nlen > 0) {
            result = malloc(nlen + 16);
            if (result) {
                snprintf(result, nlen + 16, "unsigned int : %.*s", (int)nlen, start);
            }
        } else {
            result = strdup("unsigned int");
        }
        break;
    }

    case 'n': p++; result = strdup("in");  break; /* in qualifier */
    case 'N': p++; result = strdup("inout"); break;
    case 'o': p++; result = strdup("out"); break;
    case 'O': p++; result = strdup("bycopy"); break;
    case 'R': p++; result = strdup("byref"); break;
    case 'V': p++; result = strdup("oneway"); break;
    case 'A': p++; result = strdup("_Atomic"); break;
    case 'j': p++; result = strdup("_Complex"); break;

    default:
        /* Unknown encoding character; skip it */
        p++;
        result = strdup("/* unknown */");
        break;
    }

    /* Skip trailing digits (stack offset) */
    p = skip_digits(p);

    if (is_const && result) {
        size_t len = strlen(result);
        char *cr = malloc(len + 7);
        if (cr) {
            snprintf(cr, len + 7, "const %s", result);
        }
        free(result);
        result = cr;
    }

    *cursor = p;
    return result ? result : strdup("void");
}

char *decode_type_encoding(const char *encoding)
{
    if (!encoding || !*encoding) {
        return strdup("void");
    }

    const char *p = encoding;
    return decode_single_type(&p, 0);
}

/* ---------------------------------------------------------------- */
/* Property attribute parser                                        */
/* ---------------------------------------------------------------- */

void parse_property_attributes(const char *attrs, ObjCProperty *prop)
{
    if (!attrs || !prop) return;

    /* Guard: empty string */
    if (*attrs == '\0') return;

    /* Guard: attributes should start with 'T' (type encoding) per the
     * ObjC runtime property attribute encoding specification.  If the
     * first character is not 'T', we still attempt to parse but log
     * nothing -- the caller may have a non-standard encoding. */

    prop->attributes = strdup(attrs);

    const char *p = attrs;
    while (*p) {
        char code = *p++;

        /* Find the end of this attribute (next comma or end) */
        const char *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        size_t vlen = (size_t)(end - p);

        switch (code) {
        case 'T': {
            /* Type encoding. May be the entire rest if it contains commas
               within quotes, but typically ends at the next comma. */

            /* Guard: string ends abruptly with no value after 'T' */
            if (vlen == 0) break;

            /* Decode the type */
            char *type_enc = malloc(vlen + 1);
            if (type_enc) {
                memcpy(type_enc, p, vlen);
                type_enc[vlen] = '\0';

                /* If it's an ObjC object like @"NSString", extract the class name */
                if (type_enc[0] == '@' && vlen >= 3 && type_enc[1] == '"') {
                    /* Find closing quote -- guard against unbalanced quotes */
                    char *cq = strchr(type_enc + 2, '"');
                    if (cq) {
                        size_t nlen = (size_t)(cq - (type_enc + 2));
                        prop->type_name = malloc(nlen + 3);
                        if (prop->type_name) {
                            memcpy(prop->type_name, type_enc + 2, nlen);
                            prop->type_name[nlen] = ' ';
                            prop->type_name[nlen + 1] = '*';
                            prop->type_name[nlen + 2] = '\0';
                        }
                    } else {
                        /* Unbalanced quotes: fall back to generic id */
                        prop->type_name = strdup("id");
                    }
                } else {
                    prop->type_name = decode_type_encoding(type_enc);
                }
                free(type_enc);
            }
            break;
        }
        case 'R':
            prop->is_readonly = true;
            break;
        case 'C':
            prop->is_copy = true;
            break;
        case '&':
            prop->is_retain = true;
            break;
        case 'N':
            prop->is_nonatomic = true;
            break;
        case 'W':
            prop->is_weak = true;
            break;
        case 'D':
            prop->is_dynamic = true;
            break;
        case 'G': {
            char *getter = malloc(vlen + 1);
            if (getter) {
                memcpy(getter, p, vlen);
                getter[vlen] = '\0';
                free(prop->getter);
                prop->getter = getter;
            }
            break;
        }
        case 'S': {
            char *setter = malloc(vlen + 1);
            if (setter) {
                memcpy(setter, p, vlen);
                setter[vlen] = '\0';
                free(prop->setter);
                prop->setter = setter;
            }
            break;
        }
        case 'V':
            /* ivar name -- we don't store this in the ObjCProperty struct
               but it could be useful; skip for now */
            break;
        default:
            break;
        }

        p = end;
        if (*p == '\0') break;  /* String ended abruptly mid-attribute */
        if (*p == ',') p++;
    }

    /* Default type_name if we didn't get one */
    if (!prop->type_name) {
        prop->type_name = strdup("id");
    }
}
