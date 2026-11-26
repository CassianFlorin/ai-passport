// main/app_clock.h - Wall-clock time from SNTP, for dating voice notes.
//
// The board has no battery-backed clock, so time is unknown until the first
// SNTP reply after Wi-Fi connects; callers must accept a 0 epoch.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Call periodically from a worker task. SNTP starts once, the first time the
// station reports a connection (the network interface exists only by then).
void app_clock_on_link(bool connected);

// UTC seconds, or 0 while the clock has not been set.
uint32_t app_clock_epoch(void);

// Any epoch before this is the unset clock counting from 1970.
#define APP_CLOCK_VALID_AFTER 1704067200u // 2024-01-01T00:00:00Z

static inline uint32_t app_clock_valid_or_zero(int64_t seconds)
{
    return seconds >= (int64_t)APP_CLOCK_VALID_AFTER && seconds <= (int64_t)UINT32_MAX
         ? (uint32_t)seconds : 0;
}
