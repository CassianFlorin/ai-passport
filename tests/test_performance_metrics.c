#include <assert.h>
#include <string.h>

#include "performance_metrics.h"

int main(void)
{
    char text[32];

    performance_format_bytes(text, sizeof(text), 0);
    assert(strcmp(text, "0B") == 0);
    performance_format_bytes(text, sizeof(text), 1536);
    assert(strcmp(text, "1.5K") == 0);
    performance_format_bytes(text, sizeof(text), 2U * 1024U * 1024U + 512U * 1024U);
    assert(strcmp(text, "2.5M") == 0);

    performance_format_uptime(text, sizeof(text), 3661);
    assert(strcmp(text, "01:01:01") == 0);
    performance_format_uptime(text, sizeof(text), 90061);
    assert(strcmp(text, "1d 01:01") == 0);

    assert(performance_used_percent(1000, 1000) == 0);
    assert(performance_used_percent(1000, 500) == 50);
    assert(performance_used_percent(1000, 0) == 100);
    assert(performance_used_percent(0, 0) == 0);
    assert(performance_used_percent(1000, 1200) == 0);
    return 0;
}
