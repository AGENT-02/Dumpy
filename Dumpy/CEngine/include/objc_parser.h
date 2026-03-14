#pragma once

#include "diagnostics.h"
#include "macho_reader.h"
#include "macho_sections.h"
#include "macho_vmmap.h"
#include "objc_types.h"

/*
 * Parse all ObjC metadata from a Mach-O binary.
 *
 * Populates `result` with classes, protocols, categories, and selectors.
 * Individual parse failures are recorded in `diags` without aborting
 * the entire parse.
 *
 * Returns DIAG_OK on success, DIAG_ERR_NO_OBJC_METADATA if the binary
 * contains no ObjC sections, or DIAG_WARN_PARTIAL_METADATA if some
 * items could not be parsed.
 */
DiagCode objc_parse_metadata(const MachOContext *ctx,
                             const SectionsInfo *sections,
                             const VMMap *vmmap,
                             ObjCMetadata *result,
                             DiagList *diags);
