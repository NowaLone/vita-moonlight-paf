#include "app/heap_trace.h"

#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "debug.h"

/* Diagnostic-only fixed metadata in BSS, never in the measured newlib heap. */
#define TRACE_ENTRIES 8192u
#define TRACE_BUCKETS 1024u
#define TRACE_SITES 256u
#define TRACE_TOP 12u

typedef struct TraceEntry {
    void *pointer;
    size_t bytes;
    unsigned int site;
    unsigned int next;
} TraceEntry;

typedef struct TraceSite {
    void *caller;
    size_t live_bytes;
    size_t peak_bytes;
    unsigned int live_blocks;
} TraceSite;

static TraceEntry s_entries[TRACE_ENTRIES + 1u];
static unsigned int s_buckets[TRACE_BUCKETS];
static TraceSite s_sites[TRACE_SITES];
static unsigned int s_site_count = 1u; /* Slot zero aggregates site overflow. */
static unsigned int s_free_entry;
static int s_initialized;
static volatile unsigned int s_lock;
static unsigned int s_dropped, s_unknown_frees, s_replaced, s_zero_reallocs;
static unsigned int s_failed;
static void *s_failed_caller;
static size_t s_failed_bytes;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);

static void trace_lock(void)
{
    while (__sync_lock_test_and_set(&s_lock, 1u)) {
        /* Let a preempted, lower-priority owner finish; no allocator calls. */
        sceKernelDelayThread(1);
    }
    if (!s_initialized) {
        for (unsigned int i = 1; i < TRACE_ENTRIES; ++i) {
            s_entries[i].next = i + 1u;
        }
        s_free_entry = 1u;
        s_initialized = 1;
    }
}

static void trace_unlock(void)
{
    __sync_lock_release(&s_lock);
}

/* Caller holds the metadata lock. Missing pointers can belong to libc internals. */
static TraceEntry trace_remove(void *pointer)
{
    TraceEntry removed = { 0 };
    if (!pointer) {
        return removed;
    }
    unsigned int *link = &s_buckets[((uintptr_t)pointer >> 3) % TRACE_BUCKETS];
    while (*link) {
        TraceEntry *entry = &s_entries[*link];
        if (entry->pointer == pointer) {
            unsigned int slot = *link;
            removed = *entry;
            *link = entry->next;
            s_sites[entry->site].live_bytes -= entry->bytes;
            --s_sites[entry->site].live_blocks;
            entry->pointer = NULL;
            entry->next = s_free_entry;
            s_free_entry = slot;
            return removed;
        }
        link = &entry->next;
    }
    return removed;
}

static unsigned int trace_site(void *caller)
{
    for (unsigned int i = 1; i < s_site_count; ++i) {
        if (s_sites[i].caller == caller) {
            return i;
        }
    }
    if (s_site_count == TRACE_SITES) {
        return 0;
    }
    unsigned int site = s_site_count++;
    s_sites[site].caller = caller;
    return site;
}

static void trace_insert(void *pointer, size_t bytes, unsigned int site)
{
    if (!pointer) {
        return;
    }
    /* Address reuse exposes an allocation whose free bypassed --wrap. */
    TraceEntry prior = trace_remove(pointer);
    if (prior.pointer) {
        ++s_replaced;
    }
    if (!s_free_entry) {
        ++s_dropped;
        return;
    }
    unsigned int slot = s_free_entry;
    s_free_entry = s_entries[slot].next;
    unsigned int bucket = ((uintptr_t)pointer >> 3) % TRACE_BUCKETS;
    s_entries[slot].pointer = pointer;
    s_entries[slot].bytes = bytes;
    s_entries[slot].site = site;
    s_entries[slot].next = s_buckets[bucket];
    s_buckets[bucket] = slot;
    s_sites[site].live_bytes += bytes;
    ++s_sites[site].live_blocks;
    if (s_sites[site].live_bytes > s_sites[site].peak_bytes) {
        s_sites[site].peak_bytes = s_sites[site].live_bytes;
    }
}

static void trace_result(void *pointer, size_t bytes, void *caller)
{
    trace_lock();
    if (pointer) {
        trace_insert(pointer, bytes, trace_site(caller));
    } else if (bytes) {
        ++s_failed;
        s_failed_caller = caller;
        s_failed_bytes = bytes;
    }
    trace_unlock();
}

void *__wrap_malloc(size_t size)
{
    void *pointer = __real_malloc(size);
    trace_result(pointer, size, __builtin_return_address(0));
    return pointer;
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *pointer = __real_calloc(count, size);
    size_t bytes = count && size > SIZE_MAX / count ? SIZE_MAX : count * size;
    trace_result(pointer, bytes, __builtin_return_address(0));
    return pointer;
}

void __wrap_free(void *pointer)
{
    trace_lock();
    TraceEntry old = trace_remove(pointer);
    if (pointer && !old.pointer) {
        ++s_unknown_frees;
    }
    trace_unlock();
    __real_free(pointer);
}

void *__wrap_realloc(void *pointer, size_t size)
{
    /* Detach before libc can free/reuse the address on another thread. */
    trace_lock();
    TraceEntry old = trace_remove(pointer);
    trace_unlock();
    void *result = __real_realloc(pointer, size);
    void *caller = __builtin_return_address(0);
    trace_lock();
    if (result) {
        trace_insert(result, size, trace_site(caller));
    } else if (size) {
        if (old.pointer) {
            trace_insert(old.pointer, old.bytes, old.site);
        }
        ++s_failed;
        s_failed_caller = caller;
        s_failed_bytes = size;
    } else if (pointer) {
        /* A NULL zero-size result is allocator-specific; flag lost coverage. */
        ++s_zero_reallocs;
    }
    trace_unlock();
    return result;
}

void moonlight_log_heap_trace(const char *stage)
{
    TraceSite snapshot[TRACE_SITES];
    unsigned int count, dropped, unknown, replaced, zero_reallocs, failed;
    void *failed_caller;
    size_t failed_bytes;
    trace_lock();
    memcpy(snapshot, s_sites, sizeof(snapshot));
    count = s_site_count;
    dropped = s_dropped;
    unknown = s_unknown_frees;
    replaced = s_replaced;
    zero_reallocs = s_zero_reallocs;
    failed = s_failed;
    failed_caller = s_failed_caller;
    failed_bytes = s_failed_bytes;
    trace_unlock();

    size_t live_bytes = 0;
    unsigned int live_blocks = 0;
    for (unsigned int i = 0; i < count; ++i) {
        live_bytes += snapshot[i].live_bytes;
        live_blocks += snapshot[i].live_blocks;
    }
    vita_debug_log("[Heap] %s anchor_malloc=%p sites=%u dropped=%u unknown_free=%u replaced=%u realloc_zero=%u",
        stage, (void *)&__wrap_malloc, count, dropped, unknown, replaced, zero_reallocs);
    vita_debug_log("[Heap] %s tracked_bytes=%u blocks=%u",
        stage, (unsigned int)live_bytes, live_blocks);
    vita_debug_log("[Heap] %s failures=%u last_caller=%p last_bytes=%u",
        stage, failed, failed_caller, (unsigned int)failed_bytes);
    for (unsigned int rank = 0; rank < TRACE_TOP; ++rank) {
        unsigned int best = 0;
        for (unsigned int i = 1; i < count; ++i) {
            if (snapshot[i].live_bytes > snapshot[best].live_bytes) {
                best = i;
            }
        }
        if (!snapshot[best].live_blocks) {
            break;
        }
        vita_debug_log("[Heap] %s caller=%p live_bytes=%u blocks=%u peak_bytes=%u",
            stage, snapshot[best].caller, (unsigned int)snapshot[best].live_bytes,
            snapshot[best].live_blocks, (unsigned int)snapshot[best].peak_bytes);
        snapshot[best].live_bytes = 0;
        snapshot[best].live_blocks = 0;
    }
}
