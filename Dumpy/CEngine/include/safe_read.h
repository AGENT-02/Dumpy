#ifndef SAFE_READ_H
#define SAFE_READ_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/// Check that offset+length doesn't overflow and is within total_size.
bool safe_check_range(size_t total_size, size_t offset, size_t length);

/// Copy read_size bytes from base+offset into out. Returns false if out of bounds.
bool safe_read_bytes(const uint8_t *base, size_t total_size, size_t offset,
                     void *out, size_t read_size);

/// Read a uint32_t at offset. Returns false if out of bounds.
bool safe_read_uint32(const uint8_t *base, size_t total_size, size_t offset,
                      uint32_t *out);

/// Read a uint64_t at offset. Returns false if out of bounds.
bool safe_read_uint64(const uint8_t *base, size_t total_size, size_t offset,
                      uint64_t *out);

/// Returns pointer into base if a valid null-terminated string is found
/// starting at offset, within max_len bytes and within bounds. Else NULL.
const char *safe_read_string(const uint8_t *base, size_t total_size,
                             size_t offset, size_t max_len);

#endif /* SAFE_READ_H */
