#pragma once

#include "diagnostics.h"
#include "macho_reader.h"
#include "macho_sections.h"
#include "macho_vmmap.h"
#include "swift_types.h"

/*
 * Parse Swift type metadata from a Mach-O binary.
 *
 * Reads __swift5_types, __swift5_fieldmd, and __swift5_reflstr sections
 * to extract type descriptors, field descriptors, and reflection strings.
 *
 * Populates `result` with discovered types and their fields.
 * Individual parse failures are recorded in `diags` without aborting
 * the entire parse.
 *
 * Returns DIAG_OK on success, or an error DiagCode on failure.
 */
DiagCode swift_parse_metadata(const MachOContext *ctx,
                              const SectionsInfo *sections,
                              const VMMap *vmmap,
                              SwiftMetadata *result,
                              DiagList *diags);
