#ifndef VITA_MOONLIGHT_EVENT_QUEUE_H
#define VITA_MOONLIGHT_EVENT_QUEUE_H

#include "moonlight/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MOONLIGHT_EVENT_QUEUE_CAPACITY = 16, MOONLIGHT_EVENT_SNAPSHOT_COUNT = 4 };

typedef struct MoonlightQueuedEvent {
    MoonlightEvent event;
    char address[256];
    uint64_t sequence;
} MoonlightQueuedEvent;

/* Caller serializes all access. Snapshot invalidations cannot occupy ordered slots.
 * One foreground owner must finish delivery before another conflicting flow starts. */
typedef struct MoonlightEventQueue {
    MoonlightQueuedEvent ordered[MOONLIGHT_EVENT_QUEUE_CAPACITY];
    MoonlightQueuedEvent snapshots[MOONLIGHT_EVENT_SNAPSHOT_COUNT];
    int snapshot_pending[MOONLIGHT_EVENT_SNAPSHOT_COUNT];
    int head;
    int tail;
    int count;
    uint64_t sequence;
    MoonlightQueuedEvent pending;
    int pending_valid;
} MoonlightEventQueue;

void moonlight_event_queue_reset(MoonlightEventQueue *queue);
/* 0 accepted; -1 ordered capacity exhausted (not a snapshot/discovery burst). */
int moonlight_event_queue_push(MoonlightEventQueue *queue, const MoonlightEvent *event);
/* Retains the head until acknowledge; producers cannot replace its payload/address.
 * The returned address remains valid until acknowledge/reset. */
int moonlight_event_queue_peek(MoonlightEventQueue *queue, MoonlightEvent *event);
void moonlight_event_queue_acknowledge(MoonlightEventQueue *queue);

#ifdef __cplusplus
}
#endif

#endif
