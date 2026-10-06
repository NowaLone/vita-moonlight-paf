#include <string.h>

#include <psp2/sysmodule.h>

#include "services/notification_service.h"
#include "debug.h"

namespace {

static void copy_ascii(
    SceWChar16 *destination,
    size_t capacity,
    const char *source)
{
    if (!destination || capacity == 0) {
        return;
    }

    destination[0] = 0;
    if (!source) {
        return;
    }

    size_t i = 0;
    while (source[i] != '\0' && i + 1 < capacity) {
        unsigned char ch = (unsigned char)source[i];
        destination[i] = ch < 0x80 ? (SceWChar16)ch : (SceWChar16)'?';
        ++i;
    }

    destination[i] = 0;
}

}

NotificationService::NotificationService()
    : m_initialized(false),
      m_progress_active(false)
{
}

int NotificationService::Initialize()
{
    if (m_initialized) {
        return 0;
    }

    int result = sceSysmoduleLoadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);
    if (result < 0) {
        vita_debug_log(
            "[NotificationService] load failed: 0x%08X",
            (unsigned int)result);
        return result;
    }

    m_initialized = true;
    return 0;
}

void NotificationService::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    if (m_progress_active) {
        FinishConnection(false, NULL);
    }

    sceSysmoduleUnloadModule(SCE_SYSMODULE_NOTIFICATION_UTIL);
    m_initialized = false;
}

int NotificationService::StartConnecting(const char *address)
{
    if (!m_initialized) {
        return -1;
    }

    if (m_progress_active) {
        return 0;
    }

    SceNotificationUtilProgressInitParam param;
    memset(&param, 0, sizeof(param));

    copy_ascii(
        param.notificationText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        "Connecting to PC...");
    copy_ascii(
        param.notificationSubText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        address ? address : "");
    param.separator0 = 0;
    param.separator1 = 0;
    param.unk_4EC = 0;
    param.eventHandler = NULL;

    int result = sceNotificationUtilProgressBegin(&param);
    if (result < 0) {
        vita_debug_log(
            "[NotificationService] progress begin failed: 0x%08X",
            (unsigned int)result);
        return result;
    }

    m_progress_active = true;
    return 0;
}

void NotificationService::FinishConnection(
    bool success,
    const char *address)
{
    if (!m_initialized || !m_progress_active) {
        return;
    }

    SceNotificationUtilProgressUpdateParam update;
    memset(&update, 0, sizeof(update));
    copy_ascii(
        update.notificationText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        success ? "Connected" : "Connection failed");
    copy_ascii(
        update.notificationSubText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        address ? address : "");
    update.separator0 = 0;
    update.separator1 = 0;
    update.targetProgress = 1.0f;
    sceNotificationUtilProgressUpdate(&update);

    SceNotificationUtilProgressFinishParam finish;
    memset(&finish, 0, sizeof(finish));
    copy_ascii(
        finish.notificationText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        success ? "Connected" : "Connection failed");
    copy_ascii(
        finish.notificationSubText,
        SCE_NOTIFICATIONUTIL_TEXT_MAX,
        address ? address : "");
    finish.separator0 = 0;
    finish.separator1 = 0;
    finish.path[0] = '\0';

    int result = sceNotificationUtilProgressFinish(&finish);
    if (result < 0) {
        vita_debug_log(
            "[NotificationService] progress finish failed: 0x%08X",
            (unsigned int)result);
    }

    m_progress_active = false;
}
