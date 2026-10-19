#include <psp2/appmgr.h>
#include <malloc.h>
#include <string.h>

#include "app/memory_budget.h"
#include "debug.h"

extern "C" void moonlight_log_memory_budget(const char *stage)
{
    SceAppMgrBudgetInfo budget;
    memset(&budget, 0, sizeof(budget));
    budget.size = sizeof(budget);
    int result = sceAppMgrGetBudgetInfo(&budget);

    vita_debug_log(
        "[Memory] %s config KiB: total=%u phycont=%u newlib=%u paf=%u",
        stage, (unsigned int)MOONLIGHT_SYSTEM_MEMSIZE_KIB,
        (unsigned int)MOONLIGHT_SYSTEM_PHYCONT_KIB,
        (unsigned int)MOONLIGHT_SYSTEM_NEWLIB_KIB,
        (unsigned int)MOONLIGHT_SYSTEM_PAF_KIB);

    if (result < 0) {
        vita_debug_log("[Memory] %s GetBudgetInfo failed=0x%08x",
            stage, (unsigned int)result);
    } else {
        vita_debug_log(
            "[Memory] %s mode=%d KiB free/total: user=%u/%u phycont=%u/%u cdram=%u/%u extra=%u/%u allowed=%d",
            stage, budget.app_mode,
            budget.free_user_rw / 1024u, budget.total_user_rw_mem / 1024u,
            budget.free_phycont_mem / 1024u, budget.total_phycont_mem / 1024u,
            budget.free_cdram_mem / 1024u, budget.total_cdram_mem / 1024u,
            budget.free_extra_mem / 1024u, budget.total_extra_mem / 1024u,
            (int)budget.extra_mem_allowed);
    }

    // These are newlib arena counters, not total process memory or PAF usage.
    struct mallinfo heap = mallinfo();
    vita_debug_log("[Memory] %s newlib arena bytes: arena=%u used=%u free=%u",
        stage, (unsigned int)heap.arena, (unsigned int)heap.uordblks,
        (unsigned int)heap.fordblks);
}
