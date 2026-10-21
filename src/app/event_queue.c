#include <string.h>

#include "app/event_queue.h"

static int snapshot_index(MoonlightEventType type)
{
    /* These notify readers to fetch a complete snapshot; no per-host delta/value
     * is consumed. Scan boundaries/errors and all lifecycle events remain ordered. */
    switch (type) {
    case MOONLIGHT_EVENT_HOSTS_CHANGED: return 0;
    case MOONLIGHT_EVENT_SETTINGS_CHANGED: return 1;
    default: return -1;
    }
}

static void copy_event(MoonlightQueuedEvent *out, const MoonlightEvent *event, uint64_t sequence)
{
    out->event = *event;
    out->sequence = sequence;
    if (event->address) {
        strncpy(out->address, event->address, sizeof(out->address) - 1);
        out->address[sizeof(out->address) - 1] = '\0';
        out->event.address = out->address;
    } else {
        out->address[0] = '\0';
        out->event.address = NULL;
    }
}

void moonlight_event_queue_reset(MoonlightEventQueue *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->sequence = 0;
    queue->pending_valid = 0;
    for (int i = 0; i < MOONLIGHT_EVENT_SNAPSHOT_COUNT; ++i) {
        queue->snapshot_pending[i] = 0;
    }
}

int moonlight_event_queue_push(MoonlightEventQueue *queue, const MoonlightEvent *event)
{
    int index = snapshot_index(event->type);
    if (index < 0 && queue->count == MOONLIGHT_EVENT_QUEUE_CAPACITY) return -1;

    ++queue->sequence;
    if (index >= 0) {
        /* Keep the latest invalidation and its position relative to ordered events. */
        copy_event(&queue->snapshots[index], event, queue->sequence);
        queue->snapshot_pending[index] = 1;
    } else {
        copy_event(&queue->ordered[queue->tail], event, queue->sequence);
        queue->tail = (queue->tail + 1) % MOONLIGHT_EVENT_QUEUE_CAPACITY;
        ++queue->count;
    }
    return 0;
}

int moonlight_event_queue_peek(MoonlightEventQueue *queue, MoonlightEvent *event)
{
    if (!queue->pending_valid) {
        int selected = -1;
        uint64_t sequence = 0;
        if (queue->count) sequence = queue->ordered[queue->head].sequence;
        for (int i = 0; i < MOONLIGHT_EVENT_SNAPSHOT_COUNT; ++i) {
            if (queue->snapshot_pending[i] &&
                (!sequence || queue->snapshots[i].sequence < sequence)) {
                selected = i;
                sequence = queue->snapshots[i].sequence;
            }
        }
        if (!sequence) return 0;
        if (selected >= 0) {
            copy_event(&queue->pending, &queue->snapshots[selected].event, sequence);
            queue->snapshot_pending[selected] = 0;
        } else {
            copy_event(&queue->pending, &queue->ordered[queue->head].event, sequence);
            queue->head = (queue->head + 1) % MOONLIGHT_EVENT_QUEUE_CAPACITY;
            --queue->count;
        }
        queue->pending_valid = 1;
    }
    *event = queue->pending.event;
    return 1;
}

void moonlight_event_queue_acknowledge(MoonlightEventQueue *queue)
{
    queue->pending_valid = 0;
}
