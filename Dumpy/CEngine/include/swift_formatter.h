#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "swift_types.h"

/*
 * Format a complete Swift-syntax dump of all parsed Swift types.
 * Returns a heap-allocated string (caller must free), or NULL on failure.
 */
char *format_swift_dump(const SwiftMetadata *metadata,
                        const char *binary_name,
                        const char *arch_name);
