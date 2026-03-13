#include "objc_parser.h"
#include "objc_resolver.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================ */
/* Named constants for validation limits and masks                   */
/* ================================================================ */

#define OBJC_MAX_LIST_ENTRIES    100000
#define OBJC_MAX_CLASSES         500000
#define OBJC_MAX_PROTOCOLS       500000
#define OBJC_MAX_INSTANCE_SIZE   1073741824  /* 1 GB */
#define OBJC_CLASS_DATA_MASK_64  0x00007FFFFFFFFFF8ULL
#define OBJC_CLASS_DATA_MASK_32  0xFFFFFFFCUL

/* ================================================================ */
/* Internal helpers                                                 */
/* ================================================================ */

/*
 * Read a pointer-sized value at a VM address.
 * Handles 32/64-bit and strips pointer tags.
 */
static bool read_ptr(const MachOContext *ctx, const VMMap *vmmap,
                     uint64_t vmaddr, uint64_t *out)
{
    uint64_t raw = 0;
    if (!vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                            vmaddr, ctx->is_64bit, &raw)) {
        return false;
    }
    *out = strip_pointer_tags(raw, ctx->is_64bit);
    return true;
}

/*
 * Read a swapped uint32 from a file offset.
 */
static bool read_u32_at_file(const MachOContext *ctx, size_t file_off,
                             uint32_t *out)
{
    uint32_t raw;
    if (!safe_read_uint32(ctx->data, ctx->size, file_off, &raw)) {
        return false;
    }
    *out = macho_swap32(ctx, raw);
    return true;
}

/*
 * Read a swapped uint32 at a VM address.
 */
static bool read_u32_at_vm(const MachOContext *ctx, const VMMap *vmmap,
                           uint64_t vmaddr, uint32_t *out)
{
    size_t foff;
    if (!vmmap_to_file_offset(vmmap, vmaddr, &foff)) {
        return false;
    }
    return read_u32_at_file(ctx, foff, out);
}

/* ================================================================ */
/* Method list parsing                                              */
/* ================================================================ */

static ObjCMethod *parse_method_list(const MachOContext *ctx,
                                     const VMMap *vmmap,
                                     uint64_t list_vmaddr,
                                     size_t *out_count,
                                     bool mark_class_method,
                                     DiagList *diags)
{
    *out_count = 0;
    if (list_vmaddr == 0) return NULL;

    /* method_list_t header: uint32_t entsizeAndFlags, uint32_t count */
    uint32_t entsizeAndFlags = 0, count = 0;
    if (!read_u32_at_vm(ctx, vmmap, list_vmaddr, &entsizeAndFlags) ||
        !read_u32_at_vm(ctx, vmmap, list_vmaddr + 4, &count)) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "Failed to read method list header at VM 0x%llx",
                     (unsigned long long)list_vmaddr);
        return NULL;
    }

    if (count == 0) return NULL;
    if (count > OBJC_MAX_LIST_ENTRIES) {
        diag_add(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)list_vmaddr,
                 "Method list truncated at 100000 entries");
        count = OBJC_MAX_LIST_ENTRIES;
    }

    bool is_relative = (entsizeAndFlags & METHOD_LIST_FLAG_RELATIVE) != 0;
    uint32_t entsize = entsizeAndFlags & METHOD_LIST_ENTSIZE_MASK;

    /* Validate entsize */
    if (!is_relative) {
        uint32_t expected = ctx->is_64bit ? 24 : 12;
        if (entsize == 0) {
            entsize = expected;
        } else if (entsize != expected) {
            diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)list_vmaddr,
                         "Unexpected absolute method entsize %u (expected %u); "
                         "parsing with declared entsize",
                         (unsigned)entsize, (unsigned)expected);
        }
    } else {
        if (entsize == 0) {
            entsize = 12; /* 3 x int32_t */
        } else if (entsize != 12) {
            diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)list_vmaddr,
                         "Unexpected relative method entsize %u (expected 12); "
                         "parsing with declared entsize",
                         (unsigned)entsize);
        }
    }

    ObjCMethod *methods = calloc(count, sizeof(ObjCMethod));
    if (!methods) return NULL;

    uint64_t entries_start = list_vmaddr + 8; /* past header */
    size_t parsed = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t entry_vm = entries_start + (uint64_t)i * entsize;
        ObjCMethod *m = &methods[parsed];
        m->is_class_method = mark_class_method;

        if (is_relative) {
            /* Relative method entries: three int32_t offsets */
            size_t entry_foff;
            if (!vmmap_to_file_offset(vmmap, entry_vm, &entry_foff)) {
                continue;
            }
            if (!safe_check_range(ctx->size, entry_foff, 12)) {
                continue;
            }

            int32_t name_off = 0, types_off = 0;
            /* Read raw bytes as int32_t */
            if (!safe_read_bytes(ctx->data, ctx->size, entry_foff,
                                 &name_off, 4) ||
                !safe_read_bytes(ctx->data, ctx->size, entry_foff + 4,
                                 &types_off, 4)) {
                continue;
            }

            if (ctx->needs_swap) {
                uint32_t tmp;
                int32_t swapped;
                memcpy(&tmp, &name_off, sizeof(tmp));
                tmp = macho_swap32(ctx, tmp);
                memcpy(&swapped, &tmp, sizeof(swapped));
                name_off = swapped;
                memcpy(&tmp, &types_off, sizeof(tmp));
                tmp = macho_swap32(ctx, tmp);
                memcpy(&swapped, &tmp, sizeof(swapped));
                types_off = swapped;
            }

            /*
             * For relative methods, name_offset points to a selector
             * reference (a pointer to the selector string).
             */
            uint64_t name_ref_vm = entry_vm + (int64_t)name_off;
            uint64_t sel_ptr = 0;
            if (read_ptr(ctx, vmmap, name_ref_vm, &sel_ptr) && sel_ptr != 0) {
                const char *s = resolve_string_pointer(ctx, vmmap, sel_ptr, diags);
                m->name = s ? (char *)s : NULL;
            }

            /* types_offset is relative to the types field address */
            uint64_t types_vm = entry_vm + 4 + (int64_t)types_off;
            const char *ts = vmmap_read_string(vmmap, ctx->data, ctx->size,
                                               types_vm, 1024);
            if (ts) {
                m->types = strdup(ts);
                /* NULL from strdup is acceptable for optional types field */
            }
        } else {
            /* Absolute method entries */
            if (ctx->is_64bit) {
                uint64_t name_ptr = 0, types_ptr = 0;
                if (!read_ptr(ctx, vmmap, entry_vm,      &name_ptr) ||
                    !read_ptr(ctx, vmmap, entry_vm + 8,   &types_ptr)) {
                    continue;
                }

                const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
                m->name = ns ? (char *)ns : NULL;

                const char *ts = resolve_string_pointer(ctx, vmmap, types_ptr, diags);
                m->types = ts ? (char *)ts : NULL;
            } else {
                uint64_t name_ptr = 0, types_ptr = 0;
                if (!read_ptr(ctx, vmmap, entry_vm,      &name_ptr) ||
                    !read_ptr(ctx, vmmap, entry_vm + 4,   &types_ptr)) {
                    continue;
                }

                const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
                m->name = ns ? (char *)ns : NULL;

                const char *ts = resolve_string_pointer(ctx, vmmap, types_ptr, diags);
                m->types = ts ? (char *)ts : NULL;
            }
        }

        /* Derive return type from type encoding */
        if (m->types) {
            m->return_type = decode_type_encoding(m->types);
        }

        if (!m->name) {
            /* Method name was NULL; use a placeholder instead of skipping */
            m->name = strdup("<unnamed method>");
        }

        if (m->name) {
            parsed++;
        } else {
            /* strdup failed; cleanup partial */
            free(m->types);  m->types = NULL;
            free(m->return_type); m->return_type = NULL;
        }
    }

    if (parsed == 0) {
        free(methods);
        return NULL;
    }

    *out_count = parsed;
    return methods;
}

/* ================================================================ */
/* Ivar list parsing                                                */
/* ================================================================ */

static ObjCIvar *parse_ivar_list(const MachOContext *ctx,
                                 const VMMap *vmmap,
                                 uint64_t list_vmaddr,
                                 size_t *out_count,
                                 DiagList *diags)
{
    *out_count = 0;
    if (list_vmaddr == 0) return NULL;

    uint32_t entsize = 0, count = 0;
    if (!read_u32_at_vm(ctx, vmmap, list_vmaddr, &entsize) ||
        !read_u32_at_vm(ctx, vmmap, list_vmaddr + 4, &count)) {
        return NULL;
    }

    if (count == 0) return NULL;
    if (count > OBJC_MAX_LIST_ENTRIES) {
        diag_add(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)list_vmaddr,
                 "Ivar list truncated at 100000 entries");
        count = OBJC_MAX_LIST_ENTRIES;
    }

    uint32_t expected = ctx->is_64bit ? 32 : 20;
    if (entsize == 0) entsize = expected;

    ObjCIvar *ivars = calloc(count, sizeof(ObjCIvar));
    if (!ivars) return NULL;

    uint64_t entries_start = list_vmaddr + 8;
    size_t parsed = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t entry_vm = entries_start + (uint64_t)i * entsize;
        ObjCIvar *iv = &ivars[parsed];

        if (ctx->is_64bit) {
            /* offset_ptr(8), name(8), type(8), alignment(4), size(4) */
            uint64_t offset_ptr = 0, name_ptr = 0, type_ptr = 0;
            if (!read_ptr(ctx, vmmap, entry_vm, &offset_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 8, &name_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 16, &type_ptr)) {
                continue;
            }

            /* Read the actual ivar offset value */
            if (offset_ptr != 0) {
                uint32_t ivar_off = 0;
                if (read_u32_at_vm(ctx, vmmap, offset_ptr, &ivar_off)) {
                    iv->offset = ivar_off;
                }
            }

            /* Read alignment and size from the entry */
            uint32_t align_raw = 0, ivar_size = 0;
            read_u32_at_vm(ctx, vmmap, entry_vm + 24, &align_raw);
            read_u32_at_vm(ctx, vmmap, entry_vm + 28, &ivar_size);
            iv->size = ivar_size;

            const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
            iv->name = ns ? (char *)ns : NULL;

            const char *ts = resolve_string_pointer(ctx, vmmap, type_ptr, diags);
            iv->type = ts ? (char *)ts : NULL;
        } else {
            /* 32-bit: offset_ptr(4), name(4), type(4), alignment(4), size(4) */
            uint64_t offset_ptr = 0, name_ptr = 0, type_ptr = 0;
            if (!read_ptr(ctx, vmmap, entry_vm, &offset_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 4, &name_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 8, &type_ptr)) {
                continue;
            }

            if (offset_ptr != 0) {
                uint32_t ivar_off = 0;
                if (read_u32_at_vm(ctx, vmmap, offset_ptr, &ivar_off)) {
                    iv->offset = ivar_off;
                }
            }

            uint32_t align_raw = 0, ivar_size = 0;
            read_u32_at_vm(ctx, vmmap, entry_vm + 12, &align_raw);
            read_u32_at_vm(ctx, vmmap, entry_vm + 16, &ivar_size);
            iv->size = ivar_size;

            const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
            iv->name = ns ? (char *)ns : NULL;

            const char *ts = resolve_string_pointer(ctx, vmmap, type_ptr, diags);
            iv->type = ts ? (char *)ts : NULL;
        }

        if (iv->name) {
            parsed++;
        } else {
            free(iv->type); iv->type = NULL;
        }
    }

    if (parsed == 0) {
        free(ivars);
        return NULL;
    }

    *out_count = parsed;
    return ivars;
}

/* ================================================================ */
/* Property list parsing                                            */
/* ================================================================ */

static ObjCProperty *parse_property_list(const MachOContext *ctx,
                                         const VMMap *vmmap,
                                         uint64_t list_vmaddr,
                                         size_t *out_count,
                                         DiagList *diags)
{
    *out_count = 0;
    if (list_vmaddr == 0) return NULL;

    uint32_t entsize = 0, count = 0;
    if (!read_u32_at_vm(ctx, vmmap, list_vmaddr, &entsize) ||
        !read_u32_at_vm(ctx, vmmap, list_vmaddr + 4, &count)) {
        return NULL;
    }

    if (count == 0) return NULL;
    if (count > OBJC_MAX_LIST_ENTRIES) {
        diag_add(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)list_vmaddr,
                 "Property list truncated at 100000 entries");
        count = OBJC_MAX_LIST_ENTRIES;
    }

    uint32_t expected = ctx->is_64bit ? 16 : 8;
    if (entsize == 0) entsize = expected;

    ObjCProperty *props = calloc(count, sizeof(ObjCProperty));
    if (!props) return NULL;

    uint64_t entries_start = list_vmaddr + 8;
    size_t parsed = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t entry_vm = entries_start + (uint64_t)i * entsize;
        ObjCProperty *prop = &props[parsed];

        uint64_t name_ptr = 0, attrs_ptr = 0;
        if (ctx->is_64bit) {
            if (!read_ptr(ctx, vmmap, entry_vm, &name_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 8, &attrs_ptr)) {
                continue;
            }
        } else {
            if (!read_ptr(ctx, vmmap, entry_vm, &name_ptr) ||
                !read_ptr(ctx, vmmap, entry_vm + 4, &attrs_ptr)) {
                continue;
            }
        }

        const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
        prop->name = ns ? (char *)ns : NULL;

        if (!prop->name) continue;

        const char *as = resolve_string_pointer(ctx, vmmap, attrs_ptr, diags);
        if (as) {
            parse_property_attributes(as, prop);
            free((void *)as);
        }

        parsed++;
    }

    if (parsed == 0) {
        free(props);
        return NULL;
    }

    *out_count = parsed;
    return props;
}

/* ================================================================ */
/* Protocol list parsing (list of pointers to protocol refs)        */
/* ================================================================ */

/* Forward declaration for visited-set variant */
static char **parse_protocol_ref_list_visited(
    const MachOContext *ctx, const VMMap *vmmap,
    const SectionsInfo *sections, uint64_t list_vmaddr,
    size_t *out_count, DiagList *diags,
    const uint64_t *visited, size_t visited_count);

/*
 * Maximum depth for protocol adoption traversal to detect circular references.
 */
#define OBJC_MAX_PROTOCOL_VISIT_DEPTH 64

static char **parse_protocol_ref_list(const MachOContext *ctx,
                                      const VMMap *vmmap,
                                      const SectionsInfo *sections,
                                      uint64_t list_vmaddr,
                                      size_t *out_count,
                                      DiagList *diags)
{
    return parse_protocol_ref_list_visited(ctx, vmmap, sections, list_vmaddr,
                                           out_count, diags, NULL, 0);
}

/*
 * Inner implementation with visited-set tracking to prevent infinite
 * traversal when protocols form circular adoption chains.
 */
static char **parse_protocol_ref_list_visited(
    const MachOContext *ctx,
    const VMMap *vmmap,
    const SectionsInfo *sections,
    uint64_t list_vmaddr,
    size_t *out_count,
    DiagList *diags,
    const uint64_t *visited, size_t visited_count)
{
    *out_count = 0;
    if (list_vmaddr == 0) return NULL;

    /* Check if we've already visited this list address (circular adoption) */
    for (size_t v = 0; v < visited_count; v++) {
        if (visited[v] == list_vmaddr) {
            diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                         "Circular protocol adoption detected at VM 0x%llx",
                         (unsigned long long)list_vmaddr);
            return NULL;
        }
    }

    if (visited_count >= OBJC_MAX_PROTOCOL_VISIT_DEPTH) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "Protocol adoption depth exceeded %d at VM 0x%llx",
                     OBJC_MAX_PROTOCOL_VISIT_DEPTH,
                     (unsigned long long)list_vmaddr);
        return NULL;
    }

    /*
     * protocol_list_t: uint64_t/uint32_t count, followed by count pointers
     * Actually, the count field is always a pointer-sized value.
     */
    uint64_t count_val = 0;
    if (!read_ptr(ctx, vmmap, list_vmaddr, &count_val)) {
        return NULL;
    }

    size_t count = (size_t)count_val;
    if (count == 0 || count > 10000) return NULL;

    size_t ptr_size = ctx->is_64bit ? 8 : 4;
    uint64_t entries_start = list_vmaddr + ptr_size;

    char **names = calloc(count, sizeof(char *));
    if (!names) return NULL;

    size_t parsed = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t proto_ptr = 0;
        if (!read_ptr(ctx, vmmap, entries_start + i * ptr_size, &proto_ptr)) {
            continue;
        }
        if (proto_ptr == 0) continue;

        /* Check if this specific protocol pointer was already visited */
        bool already_visited = false;
        for (size_t v = 0; v < visited_count; v++) {
            if (visited[v] == proto_ptr) {
                already_visited = true;
                break;
            }
        }
        if (already_visited) continue;

        /* Read protocol_t.name (second pointer-sized field, after isa) */
        uint64_t name_ptr = 0;
        if (!read_ptr(ctx, vmmap, proto_ptr + ptr_size, &name_ptr)) {
            continue;
        }

        const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
        if (ns) {
            names[parsed++] = (char *)ns;
        }
    }

    if (parsed == 0) {
        free(names);
        return NULL;
    }

    *out_count = parsed;
    return names;
}

/* ================================================================ */
/* Read class_ro_t fields                                           */
/* ================================================================ */

typedef struct {
    uint32_t flags;
    uint32_t instance_size;
    uint64_t name_ptr;
    uint64_t methods_ptr;
    uint64_t protocols_ptr;
    uint64_t ivars_ptr;
    uint64_t properties_ptr;
} ClassRoFields;

/*
 * class_ro_t (64-bit) layout:
 * offset  0: uint32_t flags
 * offset  4: uint32_t instanceStart
 * offset  8: uint32_t instanceSize
 * offset 12: uint32_t reserved (64-bit only)
 * offset 16: const uint8_t *ivarLayout
 * offset 24: const char *name
 * offset 32: method_list_t *baseMethods
 * offset 40: protocol_list_t *baseProtocols
 * offset 48: ivar_list_t *ivars
 * offset 56: const uint8_t *weakIvarLayout
 * offset 64: property_list_t *baseProperties
 *
 * These offsets are validated against the Apple objc4 runtime source
 * (objc-runtime-new.h).  The 64-bit struct has 4 bytes of reserved
 * padding after instanceSize, which pushes ivarLayout to offset 16
 * and every subsequent pointer-sized field by 8 bytes thereafter.
 */
static bool read_class_ro(const MachOContext *ctx, const VMMap *vmmap,
                          uint64_t ro_vmaddr, ClassRoFields *out)
{
    memset(out, 0, sizeof(*out));

    if (ctx->is_64bit) {
        /* See class_ro_t (64-bit) layout comment above read_class_ro(). */
        if (!read_u32_at_vm(ctx, vmmap, ro_vmaddr, &out->flags)) return false;
        read_u32_at_vm(ctx, vmmap, ro_vmaddr + 8, &out->instance_size);
        read_ptr(ctx, vmmap, ro_vmaddr + 24, &out->name_ptr);
        read_ptr(ctx, vmmap, ro_vmaddr + 32, &out->methods_ptr);
        read_ptr(ctx, vmmap, ro_vmaddr + 40, &out->protocols_ptr);
        read_ptr(ctx, vmmap, ro_vmaddr + 48, &out->ivars_ptr);
        read_ptr(ctx, vmmap, ro_vmaddr + 64, &out->properties_ptr);
    } else {
        /*
         * class_ro_t 32-bit layout:
         *   +0:  uint32_t flags
         *   +4:  uint32_t instance_start
         *   +8:  uint32_t instance_size
         *   +12: uint32_t ivar_layout
         *   +16: uint32_t name
         *   +20: uint32_t base_methods
         *   +24: uint32_t base_protocols
         *   +28: uint32_t ivars
         *   +32: uint32_t weak_ivar_layout
         *   +36: uint32_t base_properties
         */
        if (!read_u32_at_vm(ctx, vmmap, ro_vmaddr, &out->flags)) return false;
        read_u32_at_vm(ctx, vmmap, ro_vmaddr + 8, &out->instance_size);

        uint64_t tmp;
        if (read_ptr(ctx, vmmap, ro_vmaddr + 16, &tmp)) out->name_ptr = tmp;
        if (read_ptr(ctx, vmmap, ro_vmaddr + 20, &tmp)) out->methods_ptr = tmp;
        if (read_ptr(ctx, vmmap, ro_vmaddr + 24, &tmp)) out->protocols_ptr = tmp;
        if (read_ptr(ctx, vmmap, ro_vmaddr + 28, &tmp)) out->ivars_ptr = tmp;
        if (read_ptr(ctx, vmmap, ro_vmaddr + 36, &tmp)) out->properties_ptr = tmp;
    }

    return true;
}

/* ================================================================ */
/* Class parsing                                                    */
/* ================================================================ */

static bool parse_single_class(const MachOContext *ctx,
                               const VMMap *vmmap,
                               const SectionsInfo *sections,
                               uint64_t class_vmaddr,
                               ObjCClassInfo *cls,
                               DiagList *diags)
{
    memset(cls, 0, sizeof(*cls));

    size_t ptr_size = ctx->is_64bit ? 8 : 4;

    /*
     * ObjCClassRaw layout:
     *   isa, superclass, method_cache, vtable, data
     *   Each is pointer-sized.
     */
    uint64_t isa_ptr = 0, super_ptr = 0, data_ptr = 0;
    if (!read_ptr(ctx, vmmap, class_vmaddr, &isa_ptr)) {
        return false;
    }
    read_ptr(ctx, vmmap, class_vmaddr + ptr_size, &super_ptr);
    if (!read_ptr(ctx, vmmap, class_vmaddr + 4 * ptr_size, &data_ptr)) {
        return false;
    }

    /* Check Swift flag before masking */
    uint64_t raw_data = 0;
    vmmap_read_pointer(vmmap, ctx->data, ctx->size,
                       class_vmaddr + 4 * ptr_size, ctx->is_64bit, &raw_data);
    cls->is_swift_class = (raw_data & OBJC_CLASS_IS_SWIFT) != 0;

    /* Strip flags from data pointer using width-appropriate mask */
    if (ctx->is_64bit) {
        data_ptr = strip_pointer_tags(data_ptr & OBJC_CLASS_DATA_MASK_64,
                                      ctx->is_64bit);
    } else {
        data_ptr = data_ptr & OBJC_CLASS_DATA_MASK_32;
    }

    if (data_ptr == 0 || data_ptr < 0x1000) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "Invalid class data pointer 0x%llx at VM 0x%llx; skipping class",
                     (unsigned long long)data_ptr,
                     (unsigned long long)class_vmaddr);
        return false;
    }

    /* Read class_ro_t */
    ClassRoFields ro;
    if (!read_class_ro(ctx, vmmap, data_ptr, &ro)) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "Failed to read class_ro_t at VM 0x%llx",
                     (unsigned long long)data_ptr);
        return false;
    }

    cls->is_meta = (ro.flags & RO_META) != 0;
    cls->is_root = (ro.flags & RO_ROOT) != 0;
    cls->instance_size = ro.instance_size;

    /* Validate instance_size */
    if (ro.instance_size > OBJC_MAX_INSTANCE_SIZE) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "Unreasonable instance_size %u at VM 0x%llx",
                     (unsigned)ro.instance_size,
                     (unsigned long long)data_ptr);
    }

    /* Class name */
    const char *name = resolve_string_pointer(ctx, vmmap, ro.name_ptr, diags);
    if (name) {
        cls->name = (char *)name;
    } else {
        /* Name pointer was zero or unresolvable; use a descriptive placeholder */
        char placeholder[64];
        snprintf(placeholder, sizeof(placeholder),
                 "<unnamed class at 0x%llx>", (unsigned long long)data_ptr);
        cls->name = strdup(placeholder);
        if (!cls->name) {
            diag_add(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "strdup failed for fallback class name");
            return false;
        }
    }

    /* If this is a metaclass, skip it (we handle metaclasses inline) */
    if (cls->is_meta) {
        return true;
    }

    /* Superclass name */
    if (super_ptr != 0) {
        const char *sn = resolve_class_name(ctx, vmmap, sections,
                                            super_ptr, diags);
        cls->superclass_name = sn ? (char *)sn : NULL;
    }

    /* Instance methods */
    if (ro.methods_ptr != 0) {
        cls->instance_methods = parse_method_list(
            ctx, vmmap, ro.methods_ptr,
            &cls->instance_method_count, false, diags);
    }

    /* Ivars */
    if (ro.ivars_ptr != 0) {
        cls->ivars = parse_ivar_list(ctx, vmmap, ro.ivars_ptr,
                                     &cls->ivar_count, diags);
    }

    /* Properties */
    if (ro.properties_ptr != 0) {
        cls->properties = parse_property_list(
            ctx, vmmap, ro.properties_ptr,
            &cls->property_count, diags);
    }

    /* Protocols */
    if (ro.protocols_ptr != 0) {
        cls->protocols = parse_protocol_ref_list(
            ctx, vmmap, sections, ro.protocols_ptr,
            &cls->protocol_count, diags);
    }

    /* Metaclass -> class methods */
    if (isa_ptr != 0 && isa_ptr >= 0x1000) {
        uint64_t meta_data_ptr = 0;
        if (read_ptr(ctx, vmmap, isa_ptr + 4 * ptr_size, &meta_data_ptr)) {
            if (ctx->is_64bit) {
                meta_data_ptr = strip_pointer_tags(
                    meta_data_ptr & OBJC_CLASS_DATA_MASK_64, ctx->is_64bit);
            } else {
                meta_data_ptr = meta_data_ptr & OBJC_CLASS_DATA_MASK_32;
            }
            if (meta_data_ptr != 0 && meta_data_ptr >= 0x1000) {
                ClassRoFields meta_ro;
                if (read_class_ro(ctx, vmmap, meta_data_ptr, &meta_ro)) {
                    if (meta_ro.methods_ptr != 0) {
                        cls->class_methods = parse_method_list(
                            ctx, vmmap, meta_ro.methods_ptr,
                            &cls->class_method_count, true, diags);
                    }
                }
            }
        }
    }

    return true;
}

/* ================================================================ */
/* Parse __objc_classlist                                           */
/* ================================================================ */

static void parse_class_list(const MachOContext *ctx,
                             const VMMap *vmmap,
                             const SectionsInfo *sections,
                             ObjCMetadata *result,
                             DiagList *diags)
{
    const SectionInfo *sect = find_section(sections, "__DATA", "__objc_classlist");
    if (!sect) {
        sect = find_section(sections, "__DATA_CONST", "__objc_classlist");
    }
    if (!sect) {
        sect = find_section(sections, "__DATA_DIRTY", "__objc_classlist");
    }
    if (!sect) return;

    size_t ptr_size = ctx->is_64bit ? 8 : 4;
    size_t count = (size_t)(sect->size / ptr_size);
    if (count == 0) return;

    /* Cap at a reasonable maximum */
    if (count > OBJC_MAX_CLASSES) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, sect->offset,
                     "Class list has %zu entries; capping at %d",
                     count, OBJC_MAX_CLASSES);
        count = OBJC_MAX_CLASSES;
    }

    ObjCClassInfo *classes = calloc(count, sizeof(ObjCClassInfo));
    if (!classes) {
        diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                 "Failed to allocate class info array");
        return;
    }

    size_t parsed = 0;

    for (size_t i = 0; i < count; i++) {
        /* Read the class pointer from the list */
        size_t entry_file_off = sect->offset + i * ptr_size;
        uint64_t class_ptr = 0;

        if (ctx->is_64bit) {
            uint64_t raw;
            if (!safe_read_uint64(ctx->data, ctx->size, entry_file_off, &raw)) {
                continue;
            }
            class_ptr = strip_pointer_tags(macho_swap64(ctx, raw), true);
        } else {
            uint32_t raw;
            if (!safe_read_uint32(ctx->data, ctx->size, entry_file_off, &raw)) {
                continue;
            }
            class_ptr = macho_swap32(ctx, raw);
        }

        if (class_ptr == 0) continue;

        ObjCClassInfo cls;
        if (parse_single_class(ctx, vmmap, sections, class_ptr, &cls, diags)) {
            if (!cls.is_meta && cls.name) {
                classes[parsed++] = cls;
            } else {
                objc_class_info_destroy(&cls);
            }
        }
    }

    if (parsed == 0) {
        free(classes);
        return;
    }

    result->classes = classes;
    result->class_count = parsed;

    /* Check for duplicate class names */
    for (size_t a = 0; a < parsed; a++) {
        if (!classes[a].name) continue;
        for (size_t b = a + 1; b < parsed; b++) {
            if (!classes[b].name) continue;
            if (strcmp(classes[a].name, classes[b].name) == 0) {
                diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                             "Duplicate class name detected: %s", classes[a].name);
                break;  /* Only warn once per name */
            }
        }
    }
}

/* ================================================================ */
/* Parse __objc_protolist                                           */
/* ================================================================ */

static bool parse_single_protocol(const MachOContext *ctx,
                                  const VMMap *vmmap,
                                  const SectionsInfo *sections,
                                  uint64_t proto_vmaddr,
                                  ObjCProtocolInfo *pi,
                                  DiagList *diags)
{
    memset(pi, 0, sizeof(*pi));

    size_t ptr_size = ctx->is_64bit ? 8 : 4;

    /*
     * protocol_t layout:
     *   isa, name, protocols, instance_methods, class_methods,
     *   optional_instance_methods, optional_class_methods,
     *   instance_properties, size(u32), flags(u32)
     */
    uint64_t name_ptr = 0;
    if (!read_ptr(ctx, vmmap, proto_vmaddr + ptr_size, &name_ptr)) {
        return false;
    }

    const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);
    pi->name = ns ? (char *)ns : NULL;
    if (!pi->name) return false;

    /* Adopted protocols (with visited-set tracking for circular adoption) */
    uint64_t protos_ptr = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 2 * ptr_size, &protos_ptr) &&
        protos_ptr != 0) {
        uint64_t self_visited = proto_vmaddr;
        pi->adopted_protocols = parse_protocol_ref_list_visited(
            ctx, vmmap, sections, protos_ptr,
            &pi->adopted_protocol_count, diags,
            &self_visited, 1);
    }

    /* Instance methods */
    uint64_t imethods = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 3 * ptr_size, &imethods) &&
        imethods != 0) {
        pi->instance_methods = parse_method_list(
            ctx, vmmap, imethods, &pi->instance_method_count, false, diags);
    }

    /* Class methods */
    uint64_t cmethods = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 4 * ptr_size, &cmethods) &&
        cmethods != 0) {
        pi->class_methods = parse_method_list(
            ctx, vmmap, cmethods, &pi->class_method_count, true, diags);
    }

    /* Optional instance methods */
    uint64_t oimethods = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 5 * ptr_size, &oimethods) &&
        oimethods != 0) {
        pi->optional_instance_methods = parse_method_list(
            ctx, vmmap, oimethods, &pi->optional_instance_method_count,
            false, diags);
    }

    /* Optional class methods */
    uint64_t ocmethods = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 6 * ptr_size, &ocmethods) &&
        ocmethods != 0) {
        pi->optional_class_methods = parse_method_list(
            ctx, vmmap, ocmethods, &pi->optional_class_method_count,
            true, diags);
    }

    /* Properties */
    uint64_t props = 0;
    if (read_ptr(ctx, vmmap, proto_vmaddr + 7 * ptr_size, &props) &&
        props != 0) {
        pi->properties = parse_property_list(
            ctx, vmmap, props, &pi->property_count, diags);
    }

    return true;
}

static void parse_protocol_list(const MachOContext *ctx,
                                const VMMap *vmmap,
                                const SectionsInfo *sections,
                                ObjCMetadata *result,
                                DiagList *diags)
{
    const SectionInfo *sect = find_section(sections, "__DATA", "__objc_protolist");
    if (!sect) {
        sect = find_section(sections, "__DATA_CONST", "__objc_protolist");
    }
    if (!sect) {
        sect = find_section(sections, "__DATA_DIRTY", "__objc_protolist");
    }
    if (!sect) return;

    size_t ptr_size = ctx->is_64bit ? 8 : 4;
    size_t count = (size_t)(sect->size / ptr_size);
    if (count == 0) return;
    if (count > OBJC_MAX_PROTOCOLS) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, sect->offset,
                     "Protocol list has %zu entries; capping at %d",
                     count, OBJC_MAX_PROTOCOLS);
        count = OBJC_MAX_PROTOCOLS;
    }

    ObjCProtocolInfo *protos = calloc(count, sizeof(ObjCProtocolInfo));
    if (!protos) return;

    size_t parsed = 0;

    for (size_t i = 0; i < count; i++) {
        size_t entry_off = sect->offset + i * ptr_size;
        uint64_t proto_ptr = 0;

        if (ctx->is_64bit) {
            uint64_t raw;
            if (!safe_read_uint64(ctx->data, ctx->size, entry_off, &raw)) continue;
            proto_ptr = strip_pointer_tags(macho_swap64(ctx, raw), true);
        } else {
            uint32_t raw;
            if (!safe_read_uint32(ctx->data, ctx->size, entry_off, &raw)) continue;
            proto_ptr = macho_swap32(ctx, raw);
        }

        if (proto_ptr == 0) continue;

        ObjCProtocolInfo pi;
        if (parse_single_protocol(ctx, vmmap, sections, proto_ptr, &pi, diags)) {
            /* Deduplicate by name */
            bool dup = false;
            for (size_t j = 0; j < parsed; j++) {
                if (protos[j].name && pi.name &&
                    strcmp(protos[j].name, pi.name) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                protos[parsed++] = pi;
            } else {
                objc_protocol_info_destroy(&pi);
            }
        }
    }

    if (parsed == 0) {
        free(protos);
        return;
    }

    result->protocols = protos;
    result->protocol_count = parsed;
}

/* ================================================================ */
/* Parse __objc_catlist                                             */
/* ================================================================ */

static bool parse_single_category(const MachOContext *ctx,
                                  const VMMap *vmmap,
                                  const SectionsInfo *sections,
                                  uint64_t cat_vmaddr,
                                  ObjCCategoryInfo *cat,
                                  DiagList *diags)
{
    memset(cat, 0, sizeof(*cat));

    size_t ptr_size = ctx->is_64bit ? 8 : 4;

    /*
     * category_t layout:
     *   name, cls, instance_methods, class_methods, protocols, instance_properties
     */
    uint64_t name_ptr = 0, cls_ptr = 0;
    uint64_t imethods = 0, cmethods = 0, protos = 0, props = 0;

    read_ptr(ctx, vmmap, cat_vmaddr, &name_ptr);
    read_ptr(ctx, vmmap, cat_vmaddr + ptr_size, &cls_ptr);
    read_ptr(ctx, vmmap, cat_vmaddr + 2 * ptr_size, &imethods);
    read_ptr(ctx, vmmap, cat_vmaddr + 3 * ptr_size, &cmethods);
    read_ptr(ctx, vmmap, cat_vmaddr + 4 * ptr_size, &protos);
    read_ptr(ctx, vmmap, cat_vmaddr + 5 * ptr_size, &props);

    const char *ns = resolve_string_pointer(ctx, vmmap, name_ptr, diags);

    /* If both name and class resolve to NULL, skip with a diagnostic warning */
    if (!ns && cls_ptr == 0) {
        diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, (size_t)cat_vmaddr,
                     "Skipping category at VM 0x%llx: both name and class are NULL",
                     (unsigned long long)cat_vmaddr);
        return false;
    }

    if (ns) {
        cat->name = (char *)ns;
    } else {
        cat->name = strdup("(null)");
        if (!cat->name) {
            diag_add(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                     "strdup failed for fallback category name");
            return false;
        }
    }

    /* Resolve class name; fall back to raw pointer if unresolvable */
    if (cls_ptr != 0) {
        const char *cn = resolve_class_name(ctx, vmmap, sections,
                                            cls_ptr, diags);
        if (cn) {
            cat->class_name = (char *)cn;
        } else {
            char fallback[48];
            snprintf(fallback, sizeof(fallback),
                     "<class at 0x%llx>", (unsigned long long)cls_ptr);
            cat->class_name = strdup(fallback);
        }
    }

    /* Instance methods */
    if (imethods != 0) {
        cat->instance_methods = parse_method_list(
            ctx, vmmap, imethods, &cat->instance_method_count, false, diags);
    }

    /* Class methods */
    if (cmethods != 0) {
        cat->class_methods = parse_method_list(
            ctx, vmmap, cmethods, &cat->class_method_count, true, diags);
    }

    /* Protocols */
    if (protos != 0) {
        cat->protocols = parse_protocol_ref_list(
            ctx, vmmap, sections, protos, &cat->protocol_count, diags);
    }

    /* Properties */
    if (props != 0) {
        cat->properties = parse_property_list(
            ctx, vmmap, props, &cat->property_count, diags);
    }

    return true;
}

static void parse_category_list(const MachOContext *ctx,
                                const VMMap *vmmap,
                                const SectionsInfo *sections,
                                ObjCMetadata *result,
                                DiagList *diags)
{
    const SectionInfo *sect = find_section(sections, "__DATA", "__objc_catlist");
    if (!sect) {
        sect = find_section(sections, "__DATA_CONST", "__objc_catlist");
    }
    if (!sect) {
        sect = find_section(sections, "__DATA_DIRTY", "__objc_catlist");
    }
    if (!sect) return;

    size_t ptr_size = ctx->is_64bit ? 8 : 4;
    size_t count = (size_t)(sect->size / ptr_size);
    if (count == 0) return;
    if (count > OBJC_MAX_CLASSES) count = OBJC_MAX_CLASSES;

    ObjCCategoryInfo *cats = calloc(count, sizeof(ObjCCategoryInfo));
    if (!cats) return;

    size_t parsed = 0;

    for (size_t i = 0; i < count; i++) {
        size_t entry_off = sect->offset + i * ptr_size;
        uint64_t cat_ptr = 0;

        if (ctx->is_64bit) {
            uint64_t raw;
            if (!safe_read_uint64(ctx->data, ctx->size, entry_off, &raw)) continue;
            cat_ptr = strip_pointer_tags(macho_swap64(ctx, raw), true);
        } else {
            uint32_t raw;
            if (!safe_read_uint32(ctx->data, ctx->size, entry_off, &raw)) continue;
            cat_ptr = macho_swap32(ctx, raw);
        }

        if (cat_ptr == 0) continue;

        ObjCCategoryInfo cat;
        if (parse_single_category(ctx, vmmap, sections, cat_ptr, &cat, diags)) {
            cats[parsed++] = cat;
        }
    }

    if (parsed == 0) {
        free(cats);
        return;
    }

    result->categories = cats;
    result->category_count = parsed;
}

/* ================================================================ */
/* Parse __objc_selrefs                                             */
/* ================================================================ */

static void parse_selector_refs(const MachOContext *ctx,
                                const VMMap *vmmap,
                                ObjCMetadata *result,
                                const SectionsInfo *sections,
                                DiagList *diags)
{
    const SectionInfo *sect = find_section(sections, "__DATA", "__objc_selrefs");
    if (!sect) {
        sect = find_section(sections, "__DATA_CONST", "__objc_selrefs");
    }
    if (!sect) return;

    size_t ptr_size = ctx->is_64bit ? 8 : 4;
    size_t count = (size_t)(sect->size / ptr_size);
    if (count == 0) return;
    if (count > 1000000) count = 1000000;

    char **sels = calloc(count, sizeof(char *));
    if (!sels) return;

    size_t parsed = 0;

    for (size_t i = 0; i < count; i++) {
        size_t entry_off = sect->offset + i * ptr_size;
        uint64_t sel_ptr = 0;

        if (ctx->is_64bit) {
            uint64_t raw;
            if (!safe_read_uint64(ctx->data, ctx->size, entry_off, &raw)) continue;
            sel_ptr = strip_pointer_tags(macho_swap64(ctx, raw), true);
        } else {
            uint32_t raw;
            if (!safe_read_uint32(ctx->data, ctx->size, entry_off, &raw)) continue;
            sel_ptr = macho_swap32(ctx, raw);
        }

        if (sel_ptr == 0) continue;

        const char *s = vmmap_read_string(vmmap, ctx->data, ctx->size,
                                          sel_ptr, 4096);
        if (s && s[0]) {
            sels[parsed] = strdup(s);
            if (sels[parsed]) parsed++;
        }
    }

    if (parsed == 0) {
        free(sels);
        return;
    }

    result->selectors = sels;
    result->selector_count = parsed;
}

/* ================================================================ */
/* Cleanup / destroy functions                                      */
/* ================================================================ */

void objc_method_destroy(ObjCMethod *m)
{
    if (!m) return;
    free(m->name);
    free(m->types);
    free(m->return_type);
    m->name = NULL;
    m->types = NULL;
    m->return_type = NULL;
}

void objc_ivar_destroy(ObjCIvar *iv)
{
    if (!iv) return;
    free(iv->name);
    free(iv->type);
    iv->name = NULL;
    iv->type = NULL;
}

void objc_property_destroy(ObjCProperty *p)
{
    if (!p) return;
    free(p->name);
    free(p->attributes);
    free(p->type_name);
    free(p->getter);
    free(p->setter);
    memset(p, 0, sizeof(*p));
}

void objc_protocol_info_destroy(ObjCProtocolInfo *pi)
{
    if (!pi) return;
    free(pi->name);

    for (size_t i = 0; i < pi->instance_method_count; i++)
        objc_method_destroy(&pi->instance_methods[i]);
    free(pi->instance_methods);

    for (size_t i = 0; i < pi->class_method_count; i++)
        objc_method_destroy(&pi->class_methods[i]);
    free(pi->class_methods);

    for (size_t i = 0; i < pi->optional_instance_method_count; i++)
        objc_method_destroy(&pi->optional_instance_methods[i]);
    free(pi->optional_instance_methods);

    for (size_t i = 0; i < pi->optional_class_method_count; i++)
        objc_method_destroy(&pi->optional_class_methods[i]);
    free(pi->optional_class_methods);

    for (size_t i = 0; i < pi->property_count; i++)
        objc_property_destroy(&pi->properties[i]);
    free(pi->properties);

    for (size_t i = 0; i < pi->adopted_protocol_count; i++)
        free(pi->adopted_protocols[i]);
    free(pi->adopted_protocols);

    memset(pi, 0, sizeof(*pi));
}

void objc_class_info_destroy(ObjCClassInfo *ci)
{
    if (!ci) return;
    free(ci->name);
    free(ci->superclass_name);

    for (size_t i = 0; i < ci->instance_method_count; i++)
        objc_method_destroy(&ci->instance_methods[i]);
    free(ci->instance_methods);

    for (size_t i = 0; i < ci->class_method_count; i++)
        objc_method_destroy(&ci->class_methods[i]);
    free(ci->class_methods);

    for (size_t i = 0; i < ci->ivar_count; i++)
        objc_ivar_destroy(&ci->ivars[i]);
    free(ci->ivars);

    for (size_t i = 0; i < ci->property_count; i++)
        objc_property_destroy(&ci->properties[i]);
    free(ci->properties);

    for (size_t i = 0; i < ci->protocol_count; i++)
        free(ci->protocols[i]);
    free(ci->protocols);

    memset(ci, 0, sizeof(*ci));
}

void objc_category_info_destroy(ObjCCategoryInfo *cat)
{
    if (!cat) return;
    free(cat->name);
    free(cat->class_name);

    for (size_t i = 0; i < cat->instance_method_count; i++)
        objc_method_destroy(&cat->instance_methods[i]);
    free(cat->instance_methods);

    for (size_t i = 0; i < cat->class_method_count; i++)
        objc_method_destroy(&cat->class_methods[i]);
    free(cat->class_methods);

    for (size_t i = 0; i < cat->property_count; i++)
        objc_property_destroy(&cat->properties[i]);
    free(cat->properties);

    for (size_t i = 0; i < cat->protocol_count; i++)
        free(cat->protocols[i]);
    free(cat->protocols);

    memset(cat, 0, sizeof(*cat));
}

void objc_metadata_destroy(ObjCMetadata *meta)
{
    if (!meta) return;

    for (size_t i = 0; i < meta->class_count; i++)
        objc_class_info_destroy(&meta->classes[i]);
    free(meta->classes);

    for (size_t i = 0; i < meta->protocol_count; i++)
        objc_protocol_info_destroy(&meta->protocols[i]);
    free(meta->protocols);

    for (size_t i = 0; i < meta->category_count; i++)
        objc_category_info_destroy(&meta->categories[i]);
    free(meta->categories);

    for (size_t i = 0; i < meta->selector_count; i++)
        free(meta->selectors[i]);
    free(meta->selectors);

    memset(meta, 0, sizeof(*meta));
}

/* ================================================================ */
/* Parse __objc_imageinfo                                           */
/* ================================================================ */

static void parse_image_info(const MachOContext *ctx,
                             const SectionsInfo *sections,
                             ObjCMetadata *result,
                             DiagList *diags)
{
    (void)diags;

    /* __objc_imageinfo is 8 bytes: uint32_t version, uint32_t flags */
    const SectionInfo *sect = find_section(sections, "__DATA", "__objc_imageinfo");
    if (!sect) sect = find_section(sections, "__DATA_CONST", "__objc_imageinfo");
    if (!sect || sect->size < 8) return;

    uint32_t version = 0, flags = 0;
    if (!safe_read_uint32(ctx->data, ctx->size, (size_t)sect->offset, &version)) return;
    if (!safe_read_uint32(ctx->data, ctx->size, (size_t)sect->offset + 4, &flags)) return;

    if (ctx->needs_swap) {
        version = macho_swap32(ctx, version);
        flags = macho_swap32(ctx, flags);
    }

    result->objc_version = version;
    result->objc_flags = flags;
    result->has_image_info = true;

    /* Swift version is encoded in bits 8-15 of flags (shifted by 8) */
    uint32_t swift_bits = (flags >> 8) & 0xFF;
    result->swift_version = swift_bits;
}

/* ================================================================ */
/* Public API                                                       */
/* ================================================================ */

DiagCode objc_parse_metadata(const MachOContext *ctx,
                             const SectionsInfo *sections,
                             const VMMap *vmmap,
                             ObjCMetadata *result,
                             DiagList *diags)
{
    if (!ctx || !sections || !vmmap || !result) {
        if (diags) {
            diag_add(diags, DIAG_ERR_OBJC_PARSE_FAILED, 0,
                     "NULL argument to objc_parse_metadata");
        }
        return DIAG_ERR_OBJC_PARSE_FAILED;
    }

    memset(result, 0, sizeof(*result));

    /* Parse image info first */
    parse_image_info(ctx, sections, result, diags);

    /* Parse each section independently; failures are non-fatal */
    parse_class_list(ctx, vmmap, sections, result, diags);
    parse_protocol_list(ctx, vmmap, sections, result, diags);
    parse_category_list(ctx, vmmap, sections, result, diags);
    parse_selector_refs(ctx, vmmap, result, sections, diags);

    /* Determine result code */
    if (result->class_count == 0 && result->protocol_count == 0 &&
        result->category_count == 0 && result->selector_count == 0) {
        diag_add(diags, DIAG_ERR_NO_OBJC_METADATA, 0,
                 "No ObjC metadata found in binary");
        return DIAG_ERR_NO_OBJC_METADATA;
    }

    if (diags && diag_has_warnings(diags)) {
        return DIAG_WARN_PARTIAL_METADATA;
    }

    return DIAG_OK;
}
