// Host stub: a mutex that only checks balanced take/give.
#pragma once
#include <assert.h>
typedef int *SemaphoreHandle_t;
extern int test_mutex_depth;
static int test_mutex_storage;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &test_mutex_storage; }
static inline int xSemaphoreTake(SemaphoreHandle_t m, unsigned ticks)
{
    (void)m; (void)ticks; assert(test_mutex_depth == 0); test_mutex_depth++; return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t m)
{
    (void)m; assert(test_mutex_depth == 1); test_mutex_depth--; return 1;
}
