#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"

void *mbedtls_freertos_calloc(size_t count, size_t size)
{
    if (count != 0U && size > SIZE_MAX / count)
        return NULL;

    size_t total = count * size;
    void *ptr = pvPortMalloc(total);

    if (ptr != NULL)
        memset(ptr, 0, total);

    return ptr;
}

void mbedtls_freertos_free(void *ptr)
{
    vPortFree(ptr);
}
