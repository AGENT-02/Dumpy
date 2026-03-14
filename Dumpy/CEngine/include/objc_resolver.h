#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "macho_reader.h"
#include "macho_vmmap.h"
#include "macho_sections.h"
#include "diagnostics.h"
#include "objc_types.h"

/*
 * Resolve a VM-address pointer to a C string (class name, selector, type encoding).
 *
 * Ownership: Returns a heap-allocated copy (via strdup/malloc) that the caller
 * must free() when done. Returns NULL on failure (invalid pointer, unmapped
 * address, or allocation failure), in which case no cleanup is needed.
 */
const char *resolve_string_pointer(const MachOContext *ctx, const VMMap *vmmap,
                                   uint64_t ptr, DiagList *diags);

/*
 * Resolve a class pointer (VM address of an objc_class) to the class name string.
 *
 * Ownership: Returns a heap-allocated copy (via resolve_string_pointer) that
 * the caller must free() when done. Returns NULL on failure, in which case
 * no cleanup is needed.
 */
const char *resolve_class_name(const MachOContext *ctx, const VMMap *vmmap,
                               const SectionsInfo *sections, uint64_t class_ptr,
                               DiagList *diags);

/*
 * Parse a property attributes string (e.g. "T@\"NSString\",C,N,V_name")
 * and fill in the ObjCProperty fields.
 */
void parse_property_attributes(const char *attrs, ObjCProperty *prop);

/*
 * Decode an ObjC type encoding string to a human-readable type.
 *
 * Ownership: Returns a heap-allocated string that the caller must free().
 * Returns NULL only on allocation failure.
 */
char *decode_type_encoding(const char *encoding);

/*
 * Strip PAC / pointer authentication and other tag bits from a pointer value.
 *
 * This is the canonical implementation for PAC stripping in the project.
 * ARM64 virtual addresses are 48 bits. Top 16 bits may contain PAC tags
 * or chained fixup metadata. For chained fixup rebase pointers (bit 62 set),
 * the target address is in bits 0..50.
 *
 * strip_pac_pointer() in macho_vmmap.c delegates to this function.
 */
uint64_t strip_pointer_tags(uint64_t ptr, bool is_64bit);
