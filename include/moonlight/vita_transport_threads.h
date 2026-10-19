#ifndef VITA_MOONLIGHT_TRANSPORT_THREADS_H
#define VITA_MOONLIGHT_TRANSPORT_THREADS_H

/* Platform.c normally sets this before including any C library headers. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>

#define MOONLIGHT_TRANSPORT_STACK_BYTES (128u * 1024u)

/* Forced into common-c Platform.c only for the experimental system profile.
 * That file initializes attributes, sets stack size, and calls pthread_create
 * without checking attribute errors or destroying the attributes afterward.
 * The create adapter owns that attribute cleanup; common-c still owns context. */
int moonlight_transport_attr_init(pthread_attr_t *attr);
int moonlight_transport_attr_setstacksize(pthread_attr_t *attr, size_t requested);
int moonlight_transport_thread_create(pthread_t *thread, pthread_attr_t *attr,
    void *(*entry)(void *), void *context, const char *name);

#ifndef MOONLIGHT_TRANSPORT_THREADS_IMPLEMENTATION
/* Load pthread declarations before redirecting the three call sites. */
#define pthread_attr_init(attr) moonlight_transport_attr_init(attr)
#define pthread_attr_setstacksize(attr, size) moonlight_transport_attr_setstacksize(attr, size)
#define pthread_create(thread, attr, entry, context) \
    moonlight_transport_thread_create(thread, attr, entry, context, (context)->name)
#endif

#endif
