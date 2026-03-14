#ifndef MACHO_SECTIONS_H
#define MACHO_SECTIONS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "macho_reader.h"
#include "diagnostics.h"

typedef struct {
    char sectname[17]; /* null-terminated copy */
    char segname[17];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t flags;
    uint32_t type; /* extracted from flags & SECTION_TYPE_MASK */
} SectionInfo;

typedef struct {
    char segname[17];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    SectionInfo *sections; /* array of nsects */
} SegmentInfo;

typedef struct {
    SegmentInfo *segments;
    size_t segment_count;

    /* Quick lookup pointers for common ObjC and data sections.
       These point into the sections arrays above; do not free separately. */
    const SectionInfo *objc_classlist;
    const SectionInfo *objc_catlist;
    const SectionInfo *objc_protolist;
    const SectionInfo *objc_selrefs;
    const SectionInfo *objc_classrefs;
    const SectionInfo *objc_superrefs;
    const SectionInfo *objc_methnames;
    const SectionInfo *objc_classname;
    const SectionInfo *objc_methtype;
    const SectionInfo *cstring;
    const SectionInfo *objc_const;
    const SectionInfo *objc_data;
    const SectionInfo *data;
    const SectionInfo *data_const;

    /* Quick lookup pointers for Swift metadata sections */
    const SectionInfo *swift5_types;
    const SectionInfo *swift5_fieldmd;
    const SectionInfo *swift5_reflstr;
    const SectionInfo *swift5_proto;
    const SectionInfo *swift5_protos;
} SectionsInfo;

/// Parse all segments and sections from the Mach-O binary.
DiagCode macho_parse_sections(const MachOContext *ctx, SectionsInfo *info,
                              DiagList *diags);

/// Free all resources associated with a SectionsInfo.
void sections_info_destroy(SectionsInfo *info);

/// Find a section by segment name and section name.
const SectionInfo *find_section(const SectionsInfo *info,
                                const char *segname, const char *sectname);

#endif /* MACHO_SECTIONS_H */
