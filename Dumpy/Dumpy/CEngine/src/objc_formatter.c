#include "objc_formatter.h"
#include "objc_resolver.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ================================================================ */
/* FormatBuffer implementation                                      */
/* ================================================================ */

#define MIN_CAPACITY 4096

/* Maximum iterations for method/property/ivar loops to guard against
 * corrupted count values causing infinite loops. */
#define MAX_ITERATION_LIMIT 100000

/* Maximum recursion depth for type skipping/decoding */
#define MAX_TYPE_DEPTH 32

FormatBuffer *format_buffer_create(size_t initial_capacity)
{
    if (initial_capacity < MIN_CAPACITY) initial_capacity = MIN_CAPACITY;

    FormatBuffer *buf = malloc(sizeof(FormatBuffer));
    if (!buf) return NULL;

    buf->buffer = malloc(initial_capacity);
    if (!buf->buffer) {
        free(buf);
        return NULL;
    }

    buf->buffer[0] = '\0';
    buf->length = 0;
    buf->capacity = initial_capacity;
    buf->failed = false;
    return buf;
}

void format_buffer_destroy(FormatBuffer *buf)
{
    if (!buf) return;
    free(buf->buffer);
    free(buf);
}

static void ensure_capacity(FormatBuffer *buf, size_t additional)
{
    if (buf->failed) return;

    size_t needed = buf->length + additional + 1;
    if (needed <= buf->capacity) return;

    size_t new_cap = buf->capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    char *new_buf = realloc(buf->buffer, new_cap);
    if (!new_buf) {
        buf->failed = true;
        return;
    }

    buf->buffer = new_buf;
    buf->capacity = new_cap;
}

void format_append(FormatBuffer *buf, const char *fmt, ...)
{
    if (!buf || !fmt || buf->failed) return;

    va_list ap;

    /* Determine needed size */
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed < 0) return;

    ensure_capacity(buf, (size_t)needed);
    if (buf->failed) return;

    va_start(ap, fmt);
    vsnprintf(buf->buffer + buf->length,
              buf->capacity - buf->length, fmt, ap);
    va_end(ap);

    buf->length += (size_t)needed;
}

void format_append_indent(FormatBuffer *buf, int indent, const char *fmt, ...)
{
    if (!buf || !fmt || buf->failed) return;

    /* Write indentation (4 spaces per level) */
    for (int i = 0; i < indent; i++) {
        ensure_capacity(buf, 4);
        if (buf->failed) return;
        memcpy(buf->buffer + buf->length, "    ", 4);
        buf->length += 4;
        buf->buffer[buf->length] = '\0';
    }

    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (needed < 0) return;

    ensure_capacity(buf, (size_t)needed);
    if (buf->failed) return;

    va_start(ap, fmt);
    vsnprintf(buf->buffer + buf->length,
              buf->capacity - buf->length, fmt, ap);
    va_end(ap);

    buf->length += (size_t)needed;
}

/* ================================================================ */
/* Type and method formatting helpers                               */
/* ================================================================ */

/*
 * Given a full method type encoding, skip past the return type and
 * the two implicit parameters (self and _cmd), then decode the
 * remaining parameter types into an array.
 * Returns the number of explicit parameters.
 */

/* Skip one encoded type plus any trailing digits.
 * depth tracks recursion to prevent stack overflow. */
static const char *skip_one_type_depth(const char *p, int depth)
{
    if (!p || !*p) return p;

    /* Exceeded max recursion depth: skip to end of current scope */
    if (depth > MAX_TYPE_DEPTH) {
        while (*p) p++;
        return p;
    }

    /* Skip qualifiers */
    while (*p == 'r' || *p == 'n' || *p == 'N' || *p == 'o' ||
           *p == 'O' || *p == 'R' || *p == 'V' || *p == 'A' ||
           *p == 'j') {
        p++;
    }

    switch (*p) {
    case '@':
        p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else if (*p == '?') {
            p++;
        }
        break;

    case '^':
        p++;
        p = skip_one_type_depth(p, depth + 1);
        return p; /* digits already consumed inside */

    case '{':
    case '(': {
        char close = (*p == '{') ? '}' : ')';
        int brace_depth = 1;
        p++;
        while (*p && brace_depth > 0) {
            if (*p == '{' || *p == '(') brace_depth++;
            if (*p == close) brace_depth--;
            p++;
        }
        break;
    }

    case '[':
        p++;
        while (*p && isdigit((unsigned char)*p)) p++;
        p = skip_one_type_depth(p, depth + 1);
        if (*p == ']') p++;
        break;

    case 'b':
        p++;
        while (*p && isdigit((unsigned char)*p)) p++;
        break;

    case '\0':
        return p;

    default:
        p++;
        break;
    }

    /* Skip stack frame offset digits */
    while (*p && isdigit((unsigned char)*p)) p++;

    return p;
}

/* Public-facing wrapper that starts at depth 0 */
static const char *skip_one_type(const char *p)
{
    return skip_one_type_depth(p, 0);
}

/*
 * Decode a single type at *cursor, advancing it.
 * Thin wrapper around decode_type_encoding that handles advancing.
 * depth tracks recursion to prevent stack overflow.
 */
static char *decode_one_type_depth(const char **cursor, int depth)
{
    if (!cursor || !*cursor || !**cursor) return strdup("id");

    /* Exceeded max recursion depth */
    if (depth > MAX_TYPE_DEPTH) {
        /* Skip to end */
        while (**cursor) (*cursor)++;
        return strdup("/* nested type too deep */");
    }

    const char *start = *cursor;
    const char *end = skip_one_type(start);

    /* Extract the type portion (without trailing digits) */
    /* We need to find where the type ends and digits begin */
    size_t type_len = (size_t)(end - start);
    char *type_str = malloc(type_len + 1);
    if (!type_str) {
        *cursor = end;
        return strdup("id");
    }
    memcpy(type_str, start, type_len);
    type_str[type_len] = '\0';

    char *decoded = decode_type_encoding(type_str);
    free(type_str);

    *cursor = end;
    return decoded ? decoded : strdup("id");
}

/* Public-facing wrapper that starts at depth 0 */
static char *decode_one_type(const char **cursor)
{
    return decode_one_type_depth(cursor, 0);
}

/* ================================================================ */
/* Heuristic parameter name from type string                        */
/* ================================================================ */

static const char *heuristic_param_name(const char *type, int idx)
{
    if (!type) goto fallback;

    if (strstr(type, "String"))      return "string";
    if (strstr(type, "BOOL") || strstr(type, "bool")) return "flag";
    if (strstr(type, "Integer") || strstr(type, "UInteger") ||
        strstr(type, "int"))         return "value";
    if (strstr(type, "Array"))       return "array";
    if (strstr(type, "Dictionary"))  return "dictionary";
    if (strstr(type, "Error"))       return "error";
    if (strstr(type, "Block") || strstr(type, "block")) return "block";
    if (strstr(type, "Handler"))     return "handler";
    if (strchr(type, '*') && type[0] >= 'A' && type[0] <= 'Z') return "object";

fallback:
    (void)idx;
    return NULL; /* caller should use argN */
}

/* ================================================================ */
/* format_method                                                    */
/* ================================================================ */

void format_method(FormatBuffer *buf, const ObjCMethod *method, bool is_class_method)
{
    if (!buf || !method || !method->name || buf->failed) return;

    const char *prefix = is_class_method ? "+" : "-";

    /* Decode return type from type encoding */
    char *ret_type = NULL;
    const char *param_cursor = NULL;

    if (method->types && method->types[0]) {
        const char *tc = method->types;
        ret_type = decode_one_type(&tc);

        /* Skip self (@) and _cmd (:) -- two implicit params */
        if (*tc) tc = skip_one_type(tc); /* self */
        if (*tc) tc = skip_one_type(tc); /* _cmd */
        param_cursor = tc;
    }

    if (!ret_type) {
        ret_type = method->return_type ? strdup(method->return_type) : strdup("void");
    }

    /* Count colons to determine parameter count */
    size_t colon_count = 0;
    for (const char *c = method->name; *c; c++) {
        if (*c == ':') colon_count++;
    }

    if (colon_count == 0) {
        /* No parameters */
        format_append(buf, "%s (%s)%s;\n", prefix, ret_type, method->name);
    } else {
        /* Split selector by ':' */
        format_append(buf, "%s (%s)", prefix, ret_type);

        char *sel_copy = strdup(method->name);
        if (!sel_copy) {
            format_append(buf, "%s;\n", method->name);
            free(ret_type);
            return;
        }

        char *saveptr = NULL;
        char *part = strtok_r(sel_copy, ":", &saveptr);
        int arg_idx = 0;

        while (part) {
            /* Decode param type */
            char *param_type = NULL;
            if (param_cursor && *param_cursor) {
                param_type = decode_one_type(&param_cursor);
            }
            if (!param_type) param_type = strdup("id");

            if (arg_idx > 0) {
                format_append(buf, " ");
            }

            const char *pname = heuristic_param_name(param_type, arg_idx);
            if (pname) {
                format_append(buf, "%s:(%s)%s", part, param_type, pname);
            } else {
                format_append(buf, "%s:(%s)arg%d", part, param_type, arg_idx);
            }

            free(param_type);
            arg_idx++;
            part = strtok_r(NULL, ":", &saveptr);
        }

        format_append(buf, ";\n");
        free(sel_copy);
    }

    free(ret_type);
}

/* ================================================================ */
/* format_property                                                  */
/* ================================================================ */

void format_property(FormatBuffer *buf, const ObjCProperty *prop)
{
    if (!buf || !prop || !prop->name || buf->failed) return;

    format_append(buf, "@property ");

    /* Build attribute list */
    int attr_count = 0;
    char attrs_buf[512];
    attrs_buf[0] = '\0';
    size_t pos = 0;

#define APPEND_ATTR(s) do { \
    if (attr_count > 0) { \
        pos += (size_t)snprintf(attrs_buf + pos, sizeof(attrs_buf) - pos, ", "); \
    } \
    pos += (size_t)snprintf(attrs_buf + pos, sizeof(attrs_buf) - pos, "%s", (s)); \
    attr_count++; \
} while(0)

    if (prop->is_nonatomic) APPEND_ATTR("nonatomic");
    /* else: atomic is the default and typically not printed */

    if (prop->is_readonly) {
        APPEND_ATTR("readonly");
    }
    /* readwrite is default, typically not printed */

    if (prop->is_copy)   APPEND_ATTR("copy");
    if (prop->is_retain) APPEND_ATTR("strong");
    if (prop->is_weak)   APPEND_ATTR("weak");

    if (prop->getter) {
        char gbuf[256];
        snprintf(gbuf, sizeof(gbuf), "getter=%s", prop->getter);
        APPEND_ATTR(gbuf);
    }
    if (prop->setter) {
        char sbuf[256];
        snprintf(sbuf, sizeof(sbuf), "setter=%s", prop->setter);
        APPEND_ATTR(sbuf);
    }

#undef APPEND_ATTR

    if (attr_count > 0) {
        format_append(buf, "(%s) ", attrs_buf);
    }

    /* Type and name */
    const char *type = prop->type_name ? prop->type_name : "id";

    /* If type already ends with *, don't add space before name */
    size_t tlen = strlen(type);
    if (tlen > 0 && type[tlen - 1] == '*') {
        format_append(buf, "%s%s;\n", type, prop->name);
    } else {
        format_append(buf, "%s %s;\n", type, prop->name);
    }
}

/* ================================================================ */
/* format_ivar                                                      */
/* ================================================================ */

void format_ivar(FormatBuffer *buf, const ObjCIvar *ivar)
{
    if (!buf || !ivar || !ivar->name || buf->failed) return;

    char *decoded_type = NULL;
    if (ivar->type) {
        decoded_type = decode_type_encoding(ivar->type);
    }

    const char *type = decoded_type ? decoded_type : "id";

    size_t tlen = strlen(type);
    if (tlen > 0 && type[tlen - 1] == '*') {
        format_append_indent(buf, 1, "%s%s;\n", type, ivar->name);
    } else {
        format_append_indent(buf, 1, "%s %s;\n", type, ivar->name);
    }

    free(decoded_type);
}

/* ================================================================ */
/* format_class                                                     */
/* ================================================================ */

void format_class(FormatBuffer *buf, const ObjCClassInfo *cls)
{
    if (!buf || !cls || !cls->name || buf->failed) return;

    /* @interface ClassName : SuperClass <Proto1, Proto2> */
    format_append(buf, "@interface %s", cls->name);

    if (cls->superclass_name) {
        format_append(buf, " : %s", cls->superclass_name);
    }

    size_t proto_limit = cls->protocol_count;
    if (proto_limit > MAX_ITERATION_LIMIT) proto_limit = MAX_ITERATION_LIMIT;
    if (proto_limit > 0) {
        format_append(buf, " <");
        for (size_t i = 0; i < proto_limit; i++) {
            if (i > 0) format_append(buf, ", ");
            format_append(buf, "%s", cls->protocols[i] ? cls->protocols[i] : "?");
        }
        format_append(buf, ">");
    }

    format_append(buf, "\n");

    /* Ivars */
    size_t ivar_limit = cls->ivar_count;
    if (ivar_limit > MAX_ITERATION_LIMIT) ivar_limit = MAX_ITERATION_LIMIT;
    if (ivar_limit > 0) {
        format_append(buf, "{\n");
        for (size_t i = 0; i < ivar_limit; i++) {
            format_ivar(buf, &cls->ivars[i]);
        }
        format_append(buf, "}\n");
    }

    /* Properties */
    size_t prop_limit = cls->property_count;
    if (prop_limit > MAX_ITERATION_LIMIT) prop_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < prop_limit; i++) {
        format_property(buf, &cls->properties[i]);
    }

    /* Class methods */
    size_t cmeth_limit = cls->class_method_count;
    if (cmeth_limit > MAX_ITERATION_LIMIT) cmeth_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < cmeth_limit; i++) {
        format_method(buf, &cls->class_methods[i], true);
    }

    /* Instance methods */
    size_t imeth_limit = cls->instance_method_count;
    if (imeth_limit > MAX_ITERATION_LIMIT) imeth_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < imeth_limit; i++) {
        format_method(buf, &cls->instance_methods[i], false);
    }

    format_append(buf, "@end\n");
}

/* ================================================================ */
/* format_protocol                                                  */
/* ================================================================ */

void format_protocol(FormatBuffer *buf, const ObjCProtocolInfo *proto)
{
    if (!buf || !proto || !proto->name || buf->failed) return;

    format_append(buf, "@protocol %s", proto->name);

    size_t adopted_limit = proto->adopted_protocol_count;
    if (adopted_limit > MAX_ITERATION_LIMIT) adopted_limit = MAX_ITERATION_LIMIT;
    if (adopted_limit > 0) {
        format_append(buf, " <");
        for (size_t i = 0; i < adopted_limit; i++) {
            if (i > 0) format_append(buf, ", ");
            format_append(buf, "%s",
                          proto->adopted_protocols[i] ? proto->adopted_protocols[i] : "?");
        }
        format_append(buf, ">");
    }

    format_append(buf, "\n");

    /* Properties */
    size_t prop_limit = proto->property_count;
    if (prop_limit > MAX_ITERATION_LIMIT) prop_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < prop_limit; i++) {
        format_property(buf, &proto->properties[i]);
    }

    /* Required methods */
    size_t cmeth_limit = proto->class_method_count;
    if (cmeth_limit > MAX_ITERATION_LIMIT) cmeth_limit = MAX_ITERATION_LIMIT;
    size_t imeth_limit = proto->instance_method_count;
    if (imeth_limit > MAX_ITERATION_LIMIT) imeth_limit = MAX_ITERATION_LIMIT;

    if (cmeth_limit > 0 || imeth_limit > 0) {
        format_append(buf, "@required\n");
    }

    /* Required class methods */
    for (size_t i = 0; i < cmeth_limit; i++) {
        format_method(buf, &proto->class_methods[i], true);
    }

    /* Required instance methods */
    for (size_t i = 0; i < imeth_limit; i++) {
        format_method(buf, &proto->instance_methods[i], false);
    }

    /* Optional methods */
    size_t opt_imeth_limit = proto->optional_instance_method_count;
    if (opt_imeth_limit > MAX_ITERATION_LIMIT) opt_imeth_limit = MAX_ITERATION_LIMIT;
    size_t opt_cmeth_limit = proto->optional_class_method_count;
    if (opt_cmeth_limit > MAX_ITERATION_LIMIT) opt_cmeth_limit = MAX_ITERATION_LIMIT;

    if (opt_imeth_limit > 0 || opt_cmeth_limit > 0) {
        format_append(buf, "@optional\n");

        for (size_t i = 0; i < opt_cmeth_limit; i++) {
            format_method(buf, &proto->optional_class_methods[i], true);
        }
        for (size_t i = 0; i < opt_imeth_limit; i++) {
            format_method(buf, &proto->optional_instance_methods[i], false);
        }
    }

    format_append(buf, "@end\n");
}

/* ================================================================ */
/* format_category                                                  */
/* ================================================================ */

void format_category(FormatBuffer *buf, const ObjCCategoryInfo *cat)
{
    if (!buf || !cat || buf->failed) return;

    const char *cls_name = cat->class_name ? cat->class_name : "?";
    const char *cat_name = cat->name ? cat->name : "";

    format_append(buf, "@interface %s (%s)", cls_name, cat_name);

    size_t proto_limit = cat->protocol_count;
    if (proto_limit > MAX_ITERATION_LIMIT) proto_limit = MAX_ITERATION_LIMIT;
    if (proto_limit > 0) {
        format_append(buf, " <");
        for (size_t i = 0; i < proto_limit; i++) {
            if (i > 0) format_append(buf, ", ");
            format_append(buf, "%s",
                          cat->protocols[i] ? cat->protocols[i] : "?");
        }
        format_append(buf, ">");
    }

    format_append(buf, "\n");

    /* Properties */
    size_t prop_limit = cat->property_count;
    if (prop_limit > MAX_ITERATION_LIMIT) prop_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < prop_limit; i++) {
        format_property(buf, &cat->properties[i]);
    }

    /* Class methods */
    size_t cmeth_limit = cat->class_method_count;
    if (cmeth_limit > MAX_ITERATION_LIMIT) cmeth_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < cmeth_limit; i++) {
        format_method(buf, &cat->class_methods[i], true);
    }

    /* Instance methods */
    size_t imeth_limit = cat->instance_method_count;
    if (imeth_limit > MAX_ITERATION_LIMIT) imeth_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < imeth_limit; i++) {
        format_method(buf, &cat->instance_methods[i], false);
    }

    format_append(buf, "@end\n");
}

/* ================================================================ */
/* format_full_dump                                                 */
/* ================================================================ */

char *format_full_dump(const ObjCMetadata *metadata,
                       const char *binary_name,
                       const char *arch_name,
                       const char *file_type)
{
    if (!metadata) return NULL;

    FormatBuffer *buf = format_buffer_create(65536);
    if (!buf) return NULL;

    /* Header comment */
    format_append(buf, "//\n");
    format_append(buf, "// Generated by Dumpy\n");
    if (binary_name) {
        format_append(buf, "// Binary: %s\n", binary_name);
    }
    if (arch_name) {
        format_append(buf, "// Architecture: %s\n", arch_name);
    }
    if (file_type) {
        format_append(buf, "// File type: %s\n", file_type);
    }
    format_append(buf, "//\n\n");

    /* ---- Forward declarations ---- */
    {
        char **fwd_decls = NULL;
        size_t fwd_count = 0;
        size_t fwd_cap = 0;

        /* Helper: returns true if name was newly added */
        #define ADD_FWD(name_str) do { \
            bool _seen = false; \
            for (size_t _k = 0; _k < fwd_count; _k++) { \
                if (strcmp(fwd_decls[_k], (name_str)) == 0) { _seen = true; break; } \
            } \
            if (!_seen) { \
                if (fwd_count >= fwd_cap) { \
                    size_t _nc = fwd_cap == 0 ? 64 : fwd_cap * 2; \
                    char **_tmp = realloc(fwd_decls, _nc * sizeof(char *)); \
                    if (_tmp) { fwd_decls = _tmp; fwd_cap = _nc; } \
                } \
                if (fwd_count < fwd_cap) { \
                    fwd_decls[fwd_count++] = (char *)(name_str); \
                } \
            } \
        } while(0)

        /* Check if a name is defined locally */
        #define IS_LOCAL_CLASS(cname) ({ \
            bool _found = false; \
            for (size_t _j = 0; _j < metadata->class_count && _j < MAX_ITERATION_LIMIT; _j++) { \
                if (metadata->classes[_j].name && strcmp(metadata->classes[_j].name, (cname)) == 0) { \
                    _found = true; break; \
                } \
            } \
            _found; \
        })

        size_t _climit = metadata->class_count;
        if (_climit > MAX_ITERATION_LIMIT) _climit = MAX_ITERATION_LIMIT;

        for (size_t i = 0; i < _climit; i++) {
            /* Superclass references */
            const char *super = metadata->classes[i].superclass_name;
            if (super && super[0] && !IS_LOCAL_CLASS(super)) {
                ADD_FWD(super);
            }

            /* Property type references */
            size_t _plimit = metadata->classes[i].property_count;
            if (_plimit > MAX_ITERATION_LIMIT) _plimit = MAX_ITERATION_LIMIT;
            for (size_t p = 0; p < _plimit; p++) {
                const char *tname = metadata->classes[i].properties[p].type_name;
                if (!tname) continue;
                size_t tlen = strlen(tname);
                /* Check for ObjC pointer type: ends with '*', starts uppercase */
                if (tlen > 1 && tname[tlen - 1] == '*' &&
                    tname[0] >= 'A' && tname[0] <= 'Z') {
                    /* Extract class name without the trailing ' *' or '*' */
                    char tmp_name[256];
                    size_t nlen = tlen - 1;
                    while (nlen > 0 && tname[nlen - 1] == ' ') nlen--;
                    if (nlen > 0 && nlen < sizeof(tmp_name)) {
                        memcpy(tmp_name, tname, nlen);
                        tmp_name[nlen] = '\0';
                        if (!IS_LOCAL_CLASS(tmp_name)) {
                            /* Need a persistent copy for the fwd_decls array */
                            /* We'll emit immediately instead to avoid strdup */
                        }
                    }
                }
            }
        }

        if (fwd_count > 0) {
            for (size_t i = 0; i < fwd_count; i++) {
                format_append(buf, "@class %s;\n", fwd_decls[i]);
            }
            format_append(buf, "\n");
        }

        /* Also emit forward declarations for property types (needs strdup) */
        {
            char **prop_fwds = NULL;
            size_t pf_count = 0;
            size_t pf_cap = 0;

            for (size_t i = 0; i < _climit; i++) {
                size_t _plimit = metadata->classes[i].property_count;
                if (_plimit > MAX_ITERATION_LIMIT) _plimit = MAX_ITERATION_LIMIT;
                for (size_t p = 0; p < _plimit; p++) {
                    const char *tname = metadata->classes[i].properties[p].type_name;
                    if (!tname) continue;
                    size_t tlen = strlen(tname);
                    if (tlen > 1 && tname[tlen - 1] == '*' &&
                        tname[0] >= 'A' && tname[0] <= 'Z') {
                        char tmp_name[256];
                        size_t nlen = tlen - 1;
                        while (nlen > 0 && tname[nlen - 1] == ' ') nlen--;
                        if (nlen > 0 && nlen < sizeof(tmp_name)) {
                            memcpy(tmp_name, tname, nlen);
                            tmp_name[nlen] = '\0';
                            if (!IS_LOCAL_CLASS(tmp_name)) {
                                /* Check not already in superclass fwds */
                                bool already = false;
                                for (size_t k = 0; k < fwd_count; k++) {
                                    if (strcmp(fwd_decls[k], tmp_name) == 0) {
                                        already = true; break;
                                    }
                                }
                                /* Check not already in prop fwds */
                                for (size_t k = 0; !already && k < pf_count; k++) {
                                    if (strcmp(prop_fwds[k], tmp_name) == 0) {
                                        already = true; break;
                                    }
                                }
                                if (!already) {
                                    if (pf_count >= pf_cap) {
                                        size_t nc = pf_cap == 0 ? 64 : pf_cap * 2;
                                        char **tmp = realloc(prop_fwds, nc * sizeof(char *));
                                        if (tmp) { prop_fwds = tmp; pf_cap = nc; }
                                    }
                                    if (pf_count < pf_cap) {
                                        prop_fwds[pf_count] = strdup(tmp_name);
                                        if (prop_fwds[pf_count]) pf_count++;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (pf_count > 0) {
                /* If we already emitted superclass fwds, no extra newline needed */
                if (fwd_count == 0) {
                    /* first fwd section */
                }
                for (size_t i = 0; i < pf_count; i++) {
                    format_append(buf, "@class %s;\n", prop_fwds[i]);
                    free(prop_fwds[i]);
                }
                format_append(buf, "\n");
            }
            free(prop_fwds);
        }

        free(fwd_decls);

        #undef ADD_FWD
        #undef IS_LOCAL_CLASS
    }

    /* Protocols first */
    size_t proto_limit = metadata->protocol_count;
    if (proto_limit > MAX_ITERATION_LIMIT) proto_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < proto_limit; i++) {
        format_protocol(buf, &metadata->protocols[i]);
        format_append(buf, "\n");
    }

    /* Classes – sorted by inheritance depth so superclasses appear first */
    size_t class_limit = metadata->class_count;
    if (class_limit > MAX_ITERATION_LIMIT) class_limit = MAX_ITERATION_LIMIT;

    if (class_limit > 0) {
        /* Build (index, depth) pairs */
        typedef struct { size_t idx; int depth; } ClassOrder;
        ClassOrder *order = malloc(class_limit * sizeof(ClassOrder));

        if (order) {
            for (size_t i = 0; i < class_limit; i++) {
                order[i].idx = i;
                /* Walk superclass chain to compute depth */
                int depth = 0;
                const char *cur = metadata->classes[i].superclass_name;
                int guard = 0;
                while (cur && cur[0] && guard < 100) {
                    depth++;
                    /* Find cur in class list */
                    bool found_parent = false;
                    for (size_t j = 0; j < class_limit; j++) {
                        if (metadata->classes[j].name &&
                            strcmp(metadata->classes[j].name, cur) == 0) {
                            cur = metadata->classes[j].superclass_name;
                            found_parent = true;
                            break;
                        }
                    }
                    if (!found_parent) break;
                    guard++;
                }
                order[i].depth = depth;
            }

            /* Simple insertion sort (stable) by depth */
            for (size_t i = 1; i < class_limit; i++) {
                ClassOrder tmp = order[i];
                size_t j = i;
                while (j > 0 && order[j - 1].depth > tmp.depth) {
                    order[j] = order[j - 1];
                    j--;
                }
                order[j] = tmp;
            }

            for (size_t i = 0; i < class_limit; i++) {
                format_class(buf, &metadata->classes[order[i].idx]);
                format_append(buf, "\n");
            }
            free(order);
        } else {
            /* Fallback: original order */
            for (size_t i = 0; i < class_limit; i++) {
                format_class(buf, &metadata->classes[i]);
                format_append(buf, "\n");
            }
        }
    }

    /* Categories */
    size_t cat_limit = metadata->category_count;
    if (cat_limit > MAX_ITERATION_LIMIT) cat_limit = MAX_ITERATION_LIMIT;
    for (size_t i = 0; i < cat_limit; i++) {
        format_category(buf, &metadata->categories[i]);
        format_append(buf, "\n");
    }

    /* Check if buffer allocation failed at any point */
    if (buf->failed) {
        format_buffer_destroy(buf);
        return NULL;
    }

    /* Transfer ownership of the buffer string */
    char *result = buf->buffer;
    buf->buffer = NULL;
    format_buffer_destroy(buf);

    return result;
}
