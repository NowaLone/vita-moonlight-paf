#define MOONLIGHT_TRANSPORT_THREADS_IMPLEMENTATION
#include "moonlight/vita_transport_threads.h"

#include <errno.h>
#include <stddef.h>

#include "debug.h"

int moonlight_transport_attr_init(pthread_attr_t *attr)
{
    /* Vita's pthread_attr_t is an opaque pointer. Keep allocation failure
     * distinguishable even though common-c ignores pthread_attr_init's result. */
    *attr = NULL;
    return pthread_attr_init(attr);
}

int moonlight_transport_attr_setstacksize(pthread_attr_t *attr, size_t requested)
{
    (void)requested;
    if (*attr == NULL) {
        return ENOMEM;
    }
    return pthread_attr_setstacksize(attr, MOONLIGHT_TRANSPORT_STACK_BYTES);
}

int moonlight_transport_thread_create(pthread_t *thread, pthread_attr_t *attr,
    void *(*entry)(void *), void *context, const char *name)
{
    size_t stack_size = 0;
    int result;

    if (*attr == NULL) {
        result = ENOMEM;
    } else {
        result = pthread_attr_getstacksize(attr, &stack_size);
        if (result == 0 && stack_size != MOONLIGHT_TRANSPORT_STACK_BYTES) {
            /* Never silently fall back to the default stack after a failed
             * attribute setup. Let common-c run its normal failure unwind. */
            result = EINVAL;
        }
        if (result == 0) {
            result = pthread_create(thread, attr, entry, context);
        }
        pthread_attr_destroy(attr);
        *attr = NULL;
    }

    vita_debug_log("[Thread] %s stack_bytes=%u create_result=%d",
        name, (unsigned int)stack_size, result);
    return result;
}
