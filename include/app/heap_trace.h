#ifndef MOONLIGHT_HEAP_TRACE_H
#define MOONLIGHT_HEAP_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MOONLIGHT_HEAP_TRACE
void moonlight_log_heap_trace(const char *stage);
#else
static inline void moonlight_log_heap_trace(const char *stage) { (void)stage; }
#endif

#ifdef __cplusplus
}
#endif

#endif
