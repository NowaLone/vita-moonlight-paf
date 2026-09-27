#include <psp2/sysmodule.h>
#include <app_settings.h>
#include <paf.h>

#include "common.h"
#include "moonlight/settings.h"

void moonlight_settings_emit_closed(const MoonlightEvent *event);
void moonlight_settings_emit_changed(const MoonlightEvent *event);

namespace {

static sce::AppSettings *s_settings = NULL;
static int s_initialized = 0;
static const int kSettingsVersion = 1;

static const char *key_name(MoonlightSettingKey key)
{
    switch (key) {
    case MOONLIGHT_SETTING_RESOLUTION: return "resolution";
    case MOONLIGHT_SETTING_FPS: return "fps";
    case MOONLIGHT_SETTING_BITRATE: return "bitrate";
    case MOONLIGHT_SETTING_SOPS: return "sops";
    case MOONLIGHT_SETTING_REF_FRAME_INVALIDATION: return "ref_frame_invalidation";
    case MOONLIGHT_SETTING_STREAM_OPTIMIZATION: return "stream_optimization";
    case MOONLIGHT_SETTING_VITA_VBLANK: return "vita_vblank";
    case MOONLIGHT_SETTING_FRAME_PACER: return "frame_pacer";
    case MOONLIGHT_SETTING_LOCAL_AUDIO: return "local_audio";
    case MOONLIGHT_SETTING_DEBUG_LOG: return "debug_log";
    case MOONLIGHT_SETTING_DISABLE_POWER_SAVE: return "disable_power_save";
    case MOONLIGHT_SETTING_SWAP_XO: return "swap_xo";
    case MOONLIGHT_SETTING_SHOW_FPS: return "show_fps";
    case MOONLIGHT_SETTING_GYRO: return "gyro";
    case MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT: return "double_tap_sprint";
    case MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME: return "sprint_double_tap_time";
    case MOONLIGHT_SETTING_CONTROLLER_TYPE: return "controller_type";
    case MOONLIGHT_SETTING_SWAP_SHOULDERS: return "swap_shoulders";
    case MOONLIGHT_SETTING_MOUSE_ACCELERATION: return "mouse_acceleration";
    case MOONLIGHT_SETTING_MAPPING_ENABLED: return "mapping_enabled";
    case MOONLIGHT_SETTING_PS_BUTTON_CAPTURE: return "ps_button_capture";
    case MOONLIGHT_SETTING_FRONT_TOUCHZONES: return "front_touchzones";
    case MOONLIGHT_SETTING_TOUCHSCREEN_MODE: return "touchscreen_mode";
    case MOONLIGHT_SETTING_KEYBOARD_LAYOUT: return "keyboard_layout";
    case MOONLIGHT_SETTING_BACK_DEADZONE_TOP: return "back_deadzone_top";
    case MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT: return "back_deadzone_right";
    case MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM: return "back_deadzone_bottom";
    case MOONLIGHT_SETTING_BACK_DEADZONE_LEFT: return "back_deadzone_left";
    case MOONLIGHT_SETTING_CENTER_REGION_ONLY: return "center_region_only";
    default: return NULL;
    }
}

static void on_start_transition(const char *, int32_t) {}
static void on_page_activate(const char *, int32_t) {}
static void on_page_deactivate(const char *, int32_t) {}

static int32_t on_check_visible(const char *, bool *visible)
{
    if (visible) *visible = true;
    return SCE_OK;
}

static int32_t on_pre_create(const char *, sce::AppSettings::Element *)
{
    return SCE_OK;
}

static int32_t on_post_create(const char *, paf::ui::Widget *)
{
    return SCE_OK;
}

static int32_t on_press(const char *, const char *)
{
    MoonlightEvent event;
    event.type = MOONLIGHT_EVENT_SETTINGS_CHANGED;
    event.result = 0;
    event.host_id = -1;
    event.address = NULL;

    moonlight_settings_emit_changed(&event);
    return SCE_OK;
}

static int32_t on_press2(const char *, const char *)
{
    return SCE_OK;
}

static void on_term(int32_t result)
{
    MoonlightEvent event;
    event.type = MOONLIGHT_EVENT_SETTINGS_CLOSED;
    event.result = result;
    event.host_id = -1;
    event.address = NULL;

    moonlight_settings_emit_closed(&event);
}

static wchar_t *on_get_string(const char *element_id)
{
    if (g_plugin) {
        return g_plugin->GetString(element_id);
    }
    return NULL;
}

static int32_t on_get_surface(paf::graph::Surface **, const char *)
{
    return SCE_OK;
}

static int load_plugin()
{
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_BXCE);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_INI_FILE_PROCESSOR);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_COMMON_GUI_DIALOG);

    paf::Plugin::InitParam param;
    param.name = "app_settings_plugin";
    param.resource_file = "vs0:vsh/common/app_settings_plugin.rco";
    param.module_file = "vs0:vsh/common/app_settings.suprx";
    param.caller_name = "__main__";
    param.set_param_func = sce::AppSettings::PluginSetParamCB;
    param.init_func = sce::AppSettings::PluginInitCB;
    param.start_func = sce::AppSettings::PluginStartCB;
    param.stop_func = sce::AppSettings::PluginStopCB;
    param.exit_func = sce::AppSettings::PluginExitCB;
    param.draw_priority = 0x96;

    paf::Plugin::LoadSync(param);
    return paf::Plugin::Find("app_settings_plugin") != NULL ? 0 : -1;
}

}

int moonlight_settings_init(void)
{
    if (s_initialized) return 0;
    if (!g_plugin) return -1;

    if (load_plugin() < 0) return -1;

    size_t file_size = 0;
    const char *mime = NULL;

    paf::IDParam id("moonlight_settings");

    sce::AppSettings::InitParam param;
    param.xml_file = g_plugin->GetResource()->GetFile(id.GetIDHash(), &file_size, &mime);
    param.alloc_cb = sce_paf_malloc;
    param.free_cb = sce_paf_free;
    param.realloc_cb = sce_paf_realloc;
    param.safemem_offset = 0;
    param.safemem_size = 0x400;

    sce::AppSettings::GetInstance(param, &s_settings);
    if (s_settings == NULL) {
        return -1;
    }

    int version = 0;
    s_settings->GetInt("settings_version", &version, 0);
    if (version != kSettingsVersion) {
        s_settings->Initialize();
        s_settings->SetInt("settings_version", kSettingsVersion);
    }

    s_initialized = 1;
    return 0;
}

void moonlight_settings_shutdown(void)
{
    s_settings = NULL;
    s_initialized = 0;
}

int moonlight_settings_open(void)
{
    if (!s_initialized || s_settings == NULL) return -1;

    paf::Plugin *plugin = paf::Plugin::Find("app_settings_plugin");
    if (!plugin) return -1;

    const sce::AppSettings::Interface *iface =
        static_cast<const sce::AppSettings::Interface *>(plugin->GetInterface(1));
    if (!iface) return -1;

    sce::AppSettings::InterfaceCallbacks callbacks;
    callbacks.onStartPageTransitionCb = on_start_transition;
    callbacks.onPageActivateCb = on_page_activate;
    callbacks.onPageDeactivateCb = on_page_deactivate;
    callbacks.onCheckVisible = on_check_visible;
    callbacks.onPreCreateCb = on_pre_create;
    callbacks.onPostCreateCb = on_post_create;
    callbacks.onPressCb = on_press;
    callbacks.onPressCb2 = on_press2;
    callbacks.onTermCb = on_term;
    callbacks.onGetStringCb = on_get_string;
    callbacks.onGetSurfaceCb = on_get_surface;

    iface->Show(&callbacks);
    return 0;
}

int moonlight_settings_get_value(MoonlightSettingKey key, int *out_value)
{
    if (!s_initialized || !s_settings || !out_value) return -1;

    const char *name = key_name(key);
    if (!name) return -1;

    s_settings->GetInt(name, out_value, 0);
    return 0;
}

int moonlight_settings_set_value(MoonlightSettingKey key, int value)
{
    if (!s_initialized || !s_settings) return -1;

    const char *name = key_name(key);
    if (!name) return -1;

    s_settings->SetInt(name, value);
    return 0;
}

int moonlight_settings_get_all(MoonlightSettings *out)
{
    if (!out) return -1;

    static const int width[] = {960, 960, 1024, 1152, 1280, 1280, 1366, 1600, 1920};
    static const int height[] = {540, 544, 576, 648, 540, 720, 768, 900, 1080};

    int value = 0;
    *out = MoonlightSettings();

    moonlight_settings_get_value(MOONLIGHT_SETTING_RESOLUTION, &value);
    if (value < 0 || value > 8) value = 5;
    out->width = width[value];
    out->height = height[value];

    moonlight_settings_get_value(MOONLIGHT_SETTING_FPS, &out->fps);
    moonlight_settings_get_value(MOONLIGHT_SETTING_BITRATE, &out->bitrate);
    moonlight_settings_get_value(MOONLIGHT_SETTING_SOPS, &out->sops);
    moonlight_settings_get_value(MOONLIGHT_SETTING_LOCAL_AUDIO, &out->localaudio);
    moonlight_settings_get_value(MOONLIGHT_SETTING_FRAME_PACER, &out->enable_frame_pacer);
    moonlight_settings_get_value(MOONLIGHT_SETTING_CENTER_REGION_ONLY, &out->center_region_only);
    moonlight_settings_get_value(MOONLIGHT_SETTING_DISABLE_POWER_SAVE, &out->disable_powersave);
    moonlight_settings_get_value(MOONLIGHT_SETTING_SWAP_XO, &out->jp_layout);
    moonlight_settings_get_value(MOONLIGHT_SETTING_SHOW_FPS, &out->show_fps);
    moonlight_settings_get_value(MOONLIGHT_SETTING_DEBUG_LOG, &out->save_debug_log);
    moonlight_settings_get_value(MOONLIGHT_SETTING_FRONT_TOUCHZONES, &out->enable_front_touchzones);
    moonlight_settings_get_value(MOONLIGHT_SETTING_MAPPING_ENABLED, &out->mapping_enabled);
    moonlight_settings_get_value(MOONLIGHT_SETTING_MOUSE_ACCELERATION, &out->mouse_acceleration);
    moonlight_settings_get_value(MOONLIGHT_SETTING_REF_FRAME_INVALIDATION, &out->enable_ref_frame_invalidation);
    moonlight_settings_get_value(MOONLIGHT_SETTING_STREAM_OPTIMIZATION, &out->enable_remote_stream_optimization);
    moonlight_settings_get_value(MOONLIGHT_SETTING_VITA_VBLANK, &out->enable_vita_vblank_wait);
    moonlight_settings_get_value(MOONLIGHT_SETTING_GYRO, &out->enable_motion_controls);
    moonlight_settings_get_value(MOONLIGHT_SETTING_PS_BUTTON_CAPTURE, &out->enable_psbutton_capture);
    moonlight_settings_get_value(MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT, &out->enable_double_tap_sprint);
    moonlight_settings_get_value(MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME, &out->double_tap_sprint_step_time);
    moonlight_settings_get_value(MOONLIGHT_SETTING_CONTROLLER_TYPE, &out->controller_type);
    moonlight_settings_get_value(MOONLIGHT_SETTING_SWAP_SHOULDERS, &out->swap_shoulder_buttons);
    moonlight_settings_get_value(MOONLIGHT_SETTING_TOUCHSCREEN_MODE, &out->touchscreen_mode);
    moonlight_settings_get_value(MOONLIGHT_SETTING_KEYBOARD_LAYOUT, &out->keyboard_layout);
    moonlight_settings_get_value(MOONLIGHT_SETTING_BACK_DEADZONE_TOP, &out->back_deadzone_top);
    moonlight_settings_get_value(MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT, &out->back_deadzone_right);
    moonlight_settings_get_value(MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM, &out->back_deadzone_bottom);
    moonlight_settings_get_value(MOONLIGHT_SETTING_BACK_DEADZONE_LEFT, &out->back_deadzone_left);
    return 0;
}

void moonlight_settings_emit_closed(const MoonlightEvent *event);
void moonlight_settings_emit_changed(const MoonlightEvent *event);

