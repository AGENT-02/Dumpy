#include "macho_sections.h"
#include "macho_types.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MACHO_MAX_SECTIONS_PER_SEGMENT 10000

/* Copy up to 16 bytes and null-terminate into a 17-byte buffer. */
static void copy_name16(char dst[17], const char src[16]) {
    memcpy(dst, src, 16);
    dst[16] = '\0';
}

/* Try to set quick-lookup pointers based on section name. */
static void update_quick_lookup(SectionsInfo *info, const SectionInfo *sec) {
    const char *seg = sec->segname;
    const char *sect = sec->sectname;

    bool is_data     = (strcmp(seg, "__DATA") == 0);
    bool is_data_const = (strcmp(seg, "__DATA_CONST") == 0);

    if (is_data || is_data_const) {
        if (strcmp(sect, "__objc_classlist") == 0 && !info->objc_classlist)
            info->objc_classlist = sec;
        else if (strcmp(sect, "__objc_catlist") == 0 && !info->objc_catlist)
            info->objc_catlist = sec;
        else if (strcmp(sect, "__objc_protolist") == 0 && !info->objc_protolist)
            info->objc_protolist = sec;
        else if (strcmp(sect, "__objc_selrefs") == 0 && !info->objc_selrefs)
            info->objc_selrefs = sec;
        else if (strcmp(sect, "__objc_classrefs") == 0 && !info->objc_classrefs)
            info->objc_classrefs = sec;
        else if (strcmp(sect, "__objc_superrefs") == 0 && !info->objc_superrefs)
            info->objc_superrefs = sec;
        else if (strcmp(sect, "__objc_const") == 0 && !info->objc_const)
            info->objc_const = sec;
        else if (strcmp(sect, "__objc_data") == 0 && !info->objc_data)
            info->objc_data = sec;
    }

    if (strcmp(seg, "__TEXT") == 0) {
        if (strcmp(sect, "__objc_methnames") == 0 && !info->objc_methnames)
            info->objc_methnames = sec;
        else if (strcmp(sect, "__objc_classname") == 0 && !info->objc_classname)
            info->objc_classname = sec;
        else if (strcmp(sect, "__objc_methtype") == 0 && !info->objc_methtype)
            info->objc_methtype = sec;
        else if (strcmp(sect, "__cstring") == 0 && !info->cstring)
            info->cstring = sec;
        else if (strcmp(sect, "__swift5_types") == 0 && !info->swift5_types)
            info->swift5_types = sec;
        else if (strcmp(sect, "__swift5_fieldmd") == 0 && !info->swift5_fieldmd)
            info->swift5_fieldmd = sec;
        else if (strcmp(sect, "__swift5_reflstr") == 0 && !info->swift5_reflstr)
            info->swift5_reflstr = sec;
        else if (strcmp(sect, "__swift5_proto") == 0 && !info->swift5_proto)
            info->swift5_proto = sec;
        else if (strcmp(sect, "__swift5_protos") == 0 && !info->swift5_protos)
            info->swift5_protos = sec;
    }

    if (is_data && strcmp(sect, "__data") == 0 && !info->data)
        info->data = sec;

    if (is_data_const && !info->data_const) {
        /* Point to the segment's first section as a reference for the segment */
        /* Actually, data_const should point to a section. Use __const if available */
        if (strcmp(sect, "__const") == 0)
            info->data_const = sec;
    }
}

DiagCode macho_parse_sections(const MachOContext *ctx, SectionsInfo *info,
                              DiagList *diags) {
    if (!ctx || !info) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to macho_parse_sections");
        return DIAG_ERR_TRUNCATED;
    }

    memset(info, 0, sizeof(SectionsInfo));

    /* First pass: count segments */
    uint32_t ncmds, sizeofcmds;
    if (ctx->is_64bit) {
        MachOHeader64 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr)))
            return DIAG_ERR_TRUNCATED;
        ncmds      = macho_swap32(ctx, hdr.ncmds);
        sizeofcmds = macho_swap32(ctx, hdr.sizeofcmds);
    } else {
        MachOHeader32 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr)))
            return DIAG_ERR_TRUNCATED;
        ncmds      = macho_swap32(ctx, hdr.ncmds);
        sizeofcmds = macho_swap32(ctx, hdr.sizeofcmds);
    }

    size_t lc_start = ctx->header_size;
    if (!safe_check_range(ctx->size, lc_start, sizeofcmds))
        return DIAG_ERR_TRUNCATED;

    /* Count segments */
    size_t seg_count = 0;
    size_t offset = lc_start;
    size_t lc_end = lc_start + sizeofcmds;

    for (uint32_t i = 0; i < ncmds && offset < lc_end; i++) {
        MachOLoadCommand lc;
        if (!safe_read_bytes(ctx->data, ctx->size, offset, &lc, sizeof(lc)))
            break;
        uint32_t cmd     = macho_swap32(ctx, lc.cmd);
        uint32_t cmdsize = macho_swap32(ctx, lc.cmdsize);
        if (cmdsize < sizeof(MachOLoadCommand)) break;
        if (offset + cmdsize > lc_end) break;

        if (cmd == LC_SEGMENT || cmd == LC_SEGMENT_64)
            seg_count++;

        offset += cmdsize;
    }

    if (seg_count == 0) {
        /* No segments; that's valid (could be a weird object file) */
        return DIAG_OK;
    }

    info->segments = (SegmentInfo *)calloc(seg_count, sizeof(SegmentInfo));
    if (!info->segments) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate segments array");
        return DIAG_ERR_ALLOC_FAILED;
    }

    /* Second pass: parse segments and sections */
    offset = lc_start;
    size_t seg_idx = 0;

    for (uint32_t i = 0; i < ncmds && offset < lc_end; i++) {
        MachOLoadCommand lc;
        if (!safe_read_bytes(ctx->data, ctx->size, offset, &lc, sizeof(lc)))
            break;
        uint32_t cmd     = macho_swap32(ctx, lc.cmd);
        uint32_t cmdsize = macho_swap32(ctx, lc.cmdsize);
        if (cmdsize < sizeof(MachOLoadCommand)) break;
        if (offset + cmdsize > lc_end) break;

        if (cmd == LC_SEGMENT_64) {
            MachOSegmentCommand64 seg_cmd;
            if (cmdsize < sizeof(MachOSegmentCommand64) ||
                !safe_read_bytes(ctx->data, ctx->size, offset, &seg_cmd, sizeof(seg_cmd))) {
                if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, offset,
                                        "Cannot read LC_SEGMENT_64 at 0x%zX", offset);
                offset += cmdsize;
                continue;
            }

            SegmentInfo *seg = &info->segments[seg_idx];
            copy_name16(seg->segname, seg_cmd.segname);
            seg->vmaddr   = macho_swap64(ctx, seg_cmd.vmaddr);
            seg->vmsize   = macho_swap64(ctx, seg_cmd.vmsize);
            seg->fileoff  = macho_swap64(ctx, seg_cmd.fileoff);
            seg->filesize = macho_swap64(ctx, seg_cmd.filesize);
            seg->maxprot  = macho_swap32(ctx, seg_cmd.maxprot);
            seg->initprot = macho_swap32(ctx, seg_cmd.initprot);
            seg->nsects   = macho_swap32(ctx, seg_cmd.nsects);

            /* Parse sections */
            if (seg->nsects > 0) {
                /* Sanity check section count */
                if (seg->nsects > MACHO_MAX_SECTIONS_PER_SEGMENT) {
                    if (diags) diag_add(diags, DIAG_WARN_PARTIAL_METADATA, offset,
                                        "Segment has more than 10000 sections, truncating");
                    seg->nsects = 0;
                    seg_idx++;
                    offset += cmdsize;
                    continue;
                }

                size_t sects_offset = offset + sizeof(MachOSegmentCommand64);
                size_t sects_size = (size_t)seg->nsects * sizeof(MachOSection64);

                /* Validate sections fit within cmdsize */
                if (sects_offset + sects_size > offset + cmdsize) {
                    if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, offset,
                                            "Sections of segment %s extend beyond command",
                                            seg->segname);
                    seg->nsects = 0;
                    seg_idx++;
                    offset += cmdsize;
                    continue;
                }

                seg->sections = (SectionInfo *)calloc(seg->nsects, sizeof(SectionInfo));
                if (!seg->sections) {
                    if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                                        "Failed to allocate sections array");
                    /* Free all previously allocated segments before returning */
                    for (size_t k = 0; k < seg_idx; k++)
                        free(info->segments[k].sections);
                    free(info->segments);
                    info->segments = NULL;
                    info->segment_count = 0;
                    return DIAG_ERR_ALLOC_FAILED;
                }

                for (uint32_t s = 0; s < seg->nsects; s++) {
                    MachOSection64 sec;
                    size_t sec_off = sects_offset + (size_t)s * sizeof(MachOSection64);
                    if (!safe_read_bytes(ctx->data, ctx->size, sec_off, &sec, sizeof(sec))) {
                        if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, sec_off,
                                                "Cannot read section %u of segment %s",
                                                s, seg->segname);
                        break;
                    }

                    SectionInfo *si = &seg->sections[s];
                    copy_name16(si->sectname, sec.sectname);
                    copy_name16(si->segname, sec.segname);
                    si->addr   = macho_swap64(ctx, sec.addr);
                    si->size   = macho_swap64(ctx, sec.size);
                    si->offset = macho_swap32(ctx, sec.offset);
                    si->align  = macho_swap32(ctx, sec.align);
                    si->flags  = macho_swap32(ctx, sec.flags);
                    si->type   = si->flags & SECTION_TYPE_MASK;

                    /* Validate section bounds within segment */
                    if ((uint64_t)si->offset + si->size > seg->fileoff + seg->filesize) {
                        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, sec_off,
                                                "Section %s.%s extends beyond segment file range",
                                                si->segname, si->sectname);
                    }

                    update_quick_lookup(info, si);
                }

                /* Check for overlapping sections within this segment */
                for (uint32_t a = 0; a < seg->nsects; a++) {
                    SectionInfo *sa = &seg->sections[a];
                    if (sa->size == 0) continue;
                    uint64_t a_start = (uint64_t)sa->offset;
                    uint64_t a_end   = a_start + sa->size;
                    for (uint32_t b = a + 1; b < seg->nsects; b++) {
                        SectionInfo *sb = &seg->sections[b];
                        if (sb->size == 0) continue;
                        uint64_t b_start = (uint64_t)sb->offset;
                        uint64_t b_end   = b_start + sb->size;
                        if (a_start < b_end && b_start < a_end) {
                            if (diags) diag_add_fmt(diags, DIAG_WARN_ALIGNMENT, offset,
                                "Sections %s and %s in segment %s have overlapping file ranges",
                                sa->sectname, sb->sectname, seg->segname);
                        }
                    }
                }
            }
            seg_idx++;

        } else if (cmd == LC_SEGMENT) {
            MachOSegmentCommand32 seg_cmd;
            if (cmdsize < sizeof(MachOSegmentCommand32) ||
                !safe_read_bytes(ctx->data, ctx->size, offset, &seg_cmd, sizeof(seg_cmd))) {
                if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, offset,
                                        "Cannot read LC_SEGMENT at 0x%zX", offset);
                offset += cmdsize;
                continue;
            }

            SegmentInfo *seg = &info->segments[seg_idx];
            copy_name16(seg->segname, seg_cmd.segname);
            seg->vmaddr   = macho_swap32(ctx, seg_cmd.vmaddr);
            seg->vmsize   = macho_swap32(ctx, seg_cmd.vmsize);
            seg->fileoff  = macho_swap32(ctx, seg_cmd.fileoff);
            seg->filesize = macho_swap32(ctx, seg_cmd.filesize);
            seg->maxprot  = macho_swap32(ctx, seg_cmd.maxprot);
            seg->initprot = macho_swap32(ctx, seg_cmd.initprot);
            seg->nsects   = macho_swap32(ctx, seg_cmd.nsects);

            if (seg->nsects > 0) {
                if (seg->nsects > MACHO_MAX_SECTIONS_PER_SEGMENT) {
                    if (diags) diag_add(diags, DIAG_WARN_PARTIAL_METADATA, offset,
                                        "Segment has more than 10000 sections, truncating");
                    seg->nsects = 0;
                    seg_idx++;
                    offset += cmdsize;
                    continue;
                }

                size_t sects_offset = offset + sizeof(MachOSegmentCommand32);
                size_t sects_size = (size_t)seg->nsects * sizeof(MachOSection32);

                if (sects_offset + sects_size > offset + cmdsize) {
                    if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, offset,
                                            "Sections of segment %s extend beyond command",
                                            seg->segname);
                    seg->nsects = 0;
                    seg_idx++;
                    offset += cmdsize;
                    continue;
                }

                seg->sections = (SectionInfo *)calloc(seg->nsects, sizeof(SectionInfo));
                if (!seg->sections) {
                    if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                                        "Failed to allocate sections array");
                    /* Free all previously allocated segments before returning */
                    for (size_t k = 0; k < seg_idx; k++)
                        free(info->segments[k].sections);
                    free(info->segments);
                    info->segments = NULL;
                    info->segment_count = 0;
                    return DIAG_ERR_ALLOC_FAILED;
                }

                for (uint32_t s = 0; s < seg->nsects; s++) {
                    MachOSection32 sec;
                    size_t sec_off = sects_offset + (size_t)s * sizeof(MachOSection32);
                    if (!safe_read_bytes(ctx->data, ctx->size, sec_off, &sec, sizeof(sec))) {
                        if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_SECTION, sec_off,
                                                "Cannot read section %u of segment %s",
                                                s, seg->segname);
                        break;
                    }

                    SectionInfo *si = &seg->sections[s];
                    copy_name16(si->sectname, sec.sectname);
                    copy_name16(si->segname, sec.segname);
                    si->addr   = macho_swap32(ctx, sec.addr);
                    si->size   = macho_swap32(ctx, sec.size);
                    si->offset = macho_swap32(ctx, sec.offset);
                    si->align  = macho_swap32(ctx, sec.align);
                    si->flags  = macho_swap32(ctx, sec.flags);
                    si->type   = si->flags & SECTION_TYPE_MASK;

                    /* Validate section bounds within segment */
                    if ((uint64_t)si->offset + si->size > seg->fileoff + seg->filesize) {
                        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, sec_off,
                                                "Section %s.%s extends beyond segment file range",
                                                si->segname, si->sectname);
                    }

                    update_quick_lookup(info, si);
                }

                /* Check for overlapping sections within this segment */
                for (uint32_t a = 0; a < seg->nsects; a++) {
                    SectionInfo *sa = &seg->sections[a];
                    if (sa->size == 0) continue;
                    uint64_t a_start = (uint64_t)sa->offset;
                    uint64_t a_end   = a_start + sa->size;
                    for (uint32_t b = a + 1; b < seg->nsects; b++) {
                        SectionInfo *sb = &seg->sections[b];
                        if (sb->size == 0) continue;
                        uint64_t b_start = (uint64_t)sb->offset;
                        uint64_t b_end   = b_start + sb->size;
                        if (a_start < b_end && b_start < a_end) {
                            if (diags) diag_add_fmt(diags, DIAG_WARN_ALIGNMENT, offset,
                                "Sections %s and %s in segment %s have overlapping file ranges",
                                sa->sectname, sb->sectname, seg->segname);
                        }
                    }
                }
            }
            seg_idx++;
        }

        offset += cmdsize;
    }

    info->segment_count = seg_idx;
    return DIAG_OK;
}

void sections_info_destroy(SectionsInfo *info) {
    if (!info) return;
    for (size_t i = 0; i < info->segment_count; i++) {
        free(info->segments[i].sections);
    }
    free(info->segments);
    memset(info, 0, sizeof(SectionsInfo));
}

const SectionInfo *find_section(const SectionsInfo *info,
                                const char *segname, const char *sectname) {
    if (!info || !segname || !sectname) return NULL;

    for (size_t i = 0; i < info->segment_count; i++) {
        const SegmentInfo *seg = &info->segments[i];
        if (strcmp(seg->segname, segname) != 0) continue;

        for (uint32_t j = 0; j < seg->nsects; j++) {
            if (strcmp(seg->sections[j].sectname, sectname) == 0)
                return &seg->sections[j];
        }
    }
    return NULL;
}
