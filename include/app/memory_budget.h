#ifndef VITA_MOONLIGHT_MEMORY_BUDGET_H
#define VITA_MOONLIGHT_MEMORY_BUDGET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sparse lifecycle snapshots only; no polling or per-frame calls. */
#ifdef MOONLIGHT_SYSTEM_APP
void moonlight_log_memory_budget(const char *stage);
#else
static inline void moonlight_log_memory_budget(const char *stage)
{
    (void)stage;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
