// main/performance_metrics.c - Pure helpers for stable dashboard values.
#include "performance_metrics.h"

#include <stdio.h>

void performance_format_bytes(char *dst, size_t dst_size, size_t bytes)
{
    if (!dst || dst_size == 0) return;

    if (bytes >= 1024U * 1024U) {
        size_t whole = bytes / (1024U * 1024U);
        size_t tenth = (bytes % (1024U * 1024U)) * 10U / (1024U * 1024U);
        (void)snprintf(dst, dst_size, "%zu.%zuM", whole, tenth);
    } else if (bytes >= 1024U) {
        size_t whole = bytes / 1024U;
        size_t tenth = (bytes % 1024U) * 10U / 1024U;
        (void)snprintf(dst, dst_size, "%zu.%zuK", whole, tenth);
    } else {
        (void)snprintf(dst, dst_size, "%zuB", bytes);
    }
}

void performance_format_uptime(char *dst, size_t dst_size, uint64_t seconds)
{
    if (!dst || dst_size == 0) return;

    uint64_t days = seconds / 86400U;
    uint64_t hours = (seconds / 3600U) % 24U;
    uint64_t minutes = (seconds / 60U) % 60U;
    uint64_t secs = seconds % 60U;
    if (days > 0) {
        (void)snprintf(dst, dst_size, "%llud %02llu:%02llu",
                       (unsigned long long)days,
                       (unsigned long long)hours,
                       (unsigned long long)minutes);
    } else {
        (void)snprintf(dst, dst_size, "%02llu:%02llu:%02llu",
                       (unsigned long long)hours,
                       (unsigned long long)minutes,
                       (unsigned long long)secs);
    }
}

int performance_used_percent(size_t total_bytes, size_t free_bytes)
{
    if (total_bytes == 0 || free_bytes >= total_bytes) return 0;

    uint64_t used = (uint64_t)total_bytes - free_bytes;
    int percent = (int)((used * 100U + total_bytes / 2U) / total_bytes);
    return percent > 100 ? 100 : percent;
}
