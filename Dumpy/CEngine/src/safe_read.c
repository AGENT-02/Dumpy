#include "safe_read.h"
#include <string.h>

bool safe_check_range(size_t total_size, size_t offset, size_t length) {
    /* Check for overflow in offset + length */
    if (length > total_size) return false;
    if (offset > total_size - length) return false;
    return true;
}

bool safe_read_bytes(const uint8_t *base, size_t total_size, size_t offset,
                     void *out, size_t read_size) {
    if (!base || !out || read_size == 0) return false;
    if (!safe_check_range(total_size, offset, read_size)) return false;
    memcpy(out, base + offset, read_size);
    return true;
}

bool safe_read_uint32(const uint8_t *base, size_t total_size, size_t offset,
                      uint32_t *out) {
    return safe_read_bytes(base, total_size, offset, out, sizeof(uint32_t));
}

bool safe_read_uint64(const uint8_t *base, size_t total_size, size_t offset,
                      uint64_t *out) {
    return safe_read_bytes(base, total_size, offset, out, sizeof(uint64_t));
}

const char *safe_read_string(const uint8_t *base, size_t total_size,
                             size_t offset, size_t max_len) {
    if (!base || offset >= total_size) return NULL;

    /* Clamp max_len to remaining bytes */
    size_t remaining = total_size - offset;
    if (max_len > remaining) max_len = remaining;

    const uint8_t *start = base + offset;
    const void *nul = memchr(start, '\0', max_len);
    if (!nul) return NULL; /* no null terminator found within bounds */

    return (const char *)start;
}
