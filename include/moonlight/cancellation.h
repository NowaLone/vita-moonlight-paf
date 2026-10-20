#ifndef MOONLIGHT_CANCELLATION_H
#define MOONLIGHT_CANCELLATION_H

/* Owned by the operation/job, which outlives every worker using this token. */
typedef struct MoonlightCancellation {
    int requested;
} MoonlightCancellation;

static inline int moonlight_is_cancelled(const MoonlightCancellation *token)
{
    return token && __atomic_load_n(&token->requested, __ATOMIC_ACQUIRE);
}

static inline void moonlight_cancel(MoonlightCancellation *token)
{
    __atomic_store_n(&token->requested, 1, __ATOMIC_RELEASE);
}

#endif
