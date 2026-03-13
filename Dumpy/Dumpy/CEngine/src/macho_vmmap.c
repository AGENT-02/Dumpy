#include "macho_vmmap.h"
#include "objc_resolver.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>

/*
 * Delegate to the canonical implementation in objc_resolver.c.
 * See strip_pointer_tags() for the definitive PAC/tag stripping logic.
 */
uint64_t strip_pac_pointer(uint64_t ptr, bool is_64bit) {
    return strip_pointer_tags(ptr, is_64bit);
}

DiagCode vmmap_build(const SectionsInfo *sections, VMMap *map, DiagList *diags) {
    if (!sections || !map) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to vmmap_build");
        return DIAG_ERR_TRUNCATED;
    }

    memset(map, 0, sizeof(VMMap));

    if (sections->segment_count == 0) return DIAG_OK;

    map->regions = (VMRegion *)calloc(sections->segment_count, sizeof(VMRegion));
    if (!map->regions) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate VM regions");
        return DIAG_ERR_ALLOC_FAILED;
    }

    size_t region_count = 0;
    bool found_text = false;

    for (size_t i = 0; i < sections->segment_count; i++) {
        const SegmentInfo *seg = &sections->segments[i];

        /* Skip segments with no file mapping (e.g., __PAGEZERO, zerofill) */
        if (seg->filesize == 0 && seg->vmsize > 0) {
            /* Still record __TEXT's vmaddr even if weird */
            if (!found_text && strcmp(seg->segname, "__TEXT") == 0) {
                map->preferred_load_address = seg->vmaddr;
                found_text = true;
            }
            continue;
        }

        VMRegion *r = &map->regions[region_count];
        r->vmaddr   = seg->vmaddr;
        r->vmsize   = seg->vmsize;
        r->fileoff  = seg->fileoff;
        r->filesize = seg->filesize;
        region_count++;

        /* Record preferred load address from __TEXT segment */
        if (!found_text && strcmp(seg->segname, "__TEXT") == 0) {
            map->preferred_load_address = seg->vmaddr;
            found_text = true;
        }
    }

    map->count = region_count;

    /* Sort regions by vmaddr and check for overlaps */
    if (region_count > 1) {
        /* Simple insertion sort by vmaddr (segment counts are small) */
        for (size_t i = 1; i < region_count; i++) {
            VMRegion tmp = map->regions[i];
            size_t j = i;
            while (j > 0 && map->regions[j - 1].vmaddr > tmp.vmaddr) {
                map->regions[j] = map->regions[j - 1];
                j--;
            }
            map->regions[j] = tmp;
        }

        /* Check for overlapping regions */
        for (size_t i = 0; i + 1 < region_count; i++) {
            const VMRegion *cur  = &map->regions[i];
            const VMRegion *next = &map->regions[i + 1];
            if (cur->vmaddr + cur->vmsize > next->vmaddr) {
                if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA,
                                        (size_t)cur->vmaddr,
                                        "VM region at 0x%llX+0x%llX overlaps region at 0x%llX",
                                        (unsigned long long)cur->vmaddr,
                                        (unsigned long long)cur->vmsize,
                                        (unsigned long long)next->vmaddr);
            }
        }
    }

    return DIAG_OK;
}

void vmmap_destroy(VMMap *map) {
    if (!map) return;
    free(map->regions);
    map->regions = NULL;
    map->count = 0;
}

bool vmmap_to_file_offset(const VMMap *map, uint64_t vmaddr, size_t *file_offset) {
    if (!map || !file_offset) return false;

    for (size_t i = 0; i < map->count; i++) {
        const VMRegion *r = &map->regions[i];
        if (vmaddr >= r->vmaddr && vmaddr < r->vmaddr + r->vmsize) {
            /* Zero-fill segment: no file backing, cannot read */
            if (r->filesize == 0 && r->vmsize > 0)
                return false;

            uint64_t region_offset = vmaddr - r->vmaddr;
            /* Check that the offset falls within the file-backed portion */
            if (region_offset < r->filesize) {
                *file_offset = (size_t)(r->fileoff + region_offset);
                return true;
            }
            /* Address is in the VM range but beyond file-backed data (e.g., zerofill) */
            return false;
        }
    }
    return false;
}

bool vmmap_read_bytes(const VMMap *map, const uint8_t *data, size_t data_size,
                      uint64_t vmaddr, void *out, size_t read_size) {
    if (!map || !data || !out || read_size == 0) return false;

    size_t file_off;
    if (!vmmap_to_file_offset(map, vmaddr, &file_off)) return false;

    return safe_read_bytes(data, data_size, file_off, out, read_size);
}

bool vmmap_read_pointer(const VMMap *map, const uint8_t *data, size_t data_size,
                        uint64_t vmaddr, bool is_64bit, uint64_t *out) {
    if (!out) return false;

    if (is_64bit) {
        uint64_t raw;
        if (!vmmap_read_bytes(map, data, data_size, vmaddr, &raw, sizeof(raw)))
            return false;
        *out = strip_pac_pointer(raw, true);
        return true;
    } else {
        uint32_t raw;
        if (!vmmap_read_bytes(map, data, data_size, vmaddr, &raw, sizeof(raw)))
            return false;
        *out = (uint64_t)raw;
        return true;
    }
}

const char *vmmap_read_string(const VMMap *map, const uint8_t *data,
                               size_t data_size, uint64_t vmaddr, size_t max_len) {
    if (!map || !data) return NULL;

    size_t file_off;
    if (!vmmap_to_file_offset(map, vmaddr, &file_off)) return NULL;

    return safe_read_string(data, data_size, file_off, max_len);
}
