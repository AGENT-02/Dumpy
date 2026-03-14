#ifndef MACHO_VMMAP_H
#define MACHO_VMMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "macho_sections.h"
#include "diagnostics.h"

typedef struct {
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
} VMRegion;

typedef struct {
    VMRegion *regions;
    size_t count;
    uint64_t preferred_load_address; /* usually vmaddr of __TEXT */
} VMMap;

/// Build a VM map from parsed segments.
DiagCode vmmap_build(const SectionsInfo *sections, VMMap *map, DiagList *diags);

/// Free resources associated with a VMMap.
void vmmap_destroy(VMMap *map);

/// Convert a virtual address to a file offset. Returns false if unmapped.
bool vmmap_to_file_offset(const VMMap *map, uint64_t vmaddr, size_t *file_offset);

/// Read bytes at a virtual address.
bool vmmap_read_bytes(const VMMap *map, const uint8_t *data, size_t data_size,
                      uint64_t vmaddr, void *out, size_t read_size);

/// Read a pointer (4 or 8 bytes) at a virtual address, with PAC stripping.
bool vmmap_read_pointer(const VMMap *map, const uint8_t *data, size_t data_size,
                        uint64_t vmaddr, bool is_64bit, uint64_t *out);

/// Read a null-terminated string at a virtual address.
const char *vmmap_read_string(const VMMap *map, const uint8_t *data,
                               size_t data_size, uint64_t vmaddr, size_t max_len);

/// Strip PAC / pointer authentication bits from a 64-bit pointer.
uint64_t strip_pac_pointer(uint64_t ptr, bool is_64bit);

#endif /* MACHO_VMMAP_H */
