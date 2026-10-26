// main/performance_metrics.h - Host-testable formatting used by the live dashboard.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Format a byte count without pulling floating-point formatting into the UI.
void performance_format_bytes(char *dst, size_t dst_size, size_t bytes);

// Format elapsed seconds as HH:MM:SS or Nd HH:MM for long-running devices.
void performance_format_uptime(char *dst, size_t dst_size, uint64_t seconds);

// Return the used percentage, clamped to 0..100 for changing heap snapshots.
int performance_used_percent(size_t total_bytes, size_t free_bytes);
