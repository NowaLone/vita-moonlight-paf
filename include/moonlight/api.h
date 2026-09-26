#ifndef VITA_MOONLIGHT_API_H
#define VITA_MOONLIGHT_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum MoonlightSettingKey {
 MOONLIGHT_SETTING_RESOLUTION=0, MOONLIGHT_SETTING_FPS, MOONLIGHT_SETTING_BITRATE, MOONLIGHT_SETTING_SOPS,
 MOONLIGHT_SETTING_REF_FRAME_INVALIDATION, MOONLIGHT_SETTING_STREAM_OPTIMIZATION, MOONLIGHT_SETTING_VITA_VBLANK,
 MOONLIGHT_SETTING_FRAME_PACER, MOONLIGHT_SETTING_LOCAL_AUDIO, MOONLIGHT_SETTING_DEBUG_LOG, MOONLIGHT_SETTING_DISABLE_POWER_SAVE,
 MOONLIGHT_SETTING_SWAP_XO, MOONLIGHT_SETTING_SHOW_FPS, MOONLIGHT_SETTING_GYRO, MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT,
 MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME, MOONLIGHT_SETTING_CONTROLLER_TYPE, MOONLIGHT_SETTING_SWAP_SHOULDERS,
 MOONLIGHT_SETTING_MOUSE_ACCELERATION, MOONLIGHT_SETTING_MAPPING_ENABLED, MOONLIGHT_SETTING_PS_BUTTON_CAPTURE,
 MOONLIGHT_SETTING_FRONT_TOUCHZONES, MOONLIGHT_SETTING_TOUCHSCREEN_MODE, MOONLIGHT_SETTING_KEYBOARD_LAYOUT,
 MOONLIGHT_SETTING_BACK_DEADZONE_TOP, MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT, MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM,
 MOONLIGHT_SETTING_BACK_DEADZONE_LEFT, MOONLIGHT_SETTING_CENTER_REGION_ONLY, MOONLIGHT_SETTING_MOTION_SCALAR_X,
 MOONLIGHT_SETTING_MOTION_SCALAR_Y, MOONLIGHT_SETTING_COUNT
} MoonlightSettingKey;
typedef struct MoonlightSettings {
 int width,height,fps,bitrate,sops,localaudio,enable_frame_pacer,center_region_only,disable_powersave,jp_layout,show_fps,save_debug_log,
 enable_front_touchzones,mouse_acceleration,enable_ref_frame_invalidation,enable_remote_stream_optimization,enable_vita_vblank_wait,
 enable_motion_controls,enable_psbutton_capture,enable_double_tap_sprint,double_tap_sprint_step_time;
 float motion_controls_scalar_x,motion_controls_scalar_y;
 int keyboard_layout,touchscreen_mode,controller_type,swap_shoulder_buttons;
 int back_deadzone_top,back_deadzone_right,back_deadzone_bottom,back_deadzone_left;
} MoonlightSettings;
typedef struct MoonlightHost {
 int id; char name[256]; char internal[256]; char external[256]; char mac[18]; uint16_t port; int paired; int online; int prefer_external;
} MoonlightHost;
typedef enum MoonlightEventType {
 MOONLIGHT_EVENT_NONE=0,MOONLIGHT_EVENT_HOST_SCAN_STARTED,MOONLIGHT_EVENT_HOST_SCAN_FINISHED,MOONLIGHT_EVENT_HOST_SCAN_FAILED,
 MOONLIGHT_EVENT_HOSTS_CHANGED,MOONLIGHT_EVENT_PAIRING_REQUIRED,MOONLIGHT_EVENT_PAIRING_FINISHED,MOONLIGHT_EVENT_PAIRING_FAILED,
 MOONLIGHT_EVENT_CONNECTION_STARTED,MOONLIGHT_EVENT_CONNECTION_READY,MOONLIGHT_EVENT_CONNECTION_FAILED,MOONLIGHT_EVENT_CONNECTION_CLOSED,
 MOONLIGHT_EVENT_STREAM_STARTED,MOONLIGHT_EVENT_STREAM_STOPPED,MOONLIGHT_EVENT_STREAM_FAILED,MOONLIGHT_EVENT_SETTINGS_CHANGED
} MoonlightEventType;
typedef struct MoonlightEvent { MoonlightEventType type; int result; int host_id; const char *address; } MoonlightEvent;
typedef void (*MoonlightEventCallback)(const MoonlightEvent*,void*);
int moonlight_api_init(void); void moonlight_api_shutdown(void); const char* moonlight_api_config_path(void);
int moonlight_api_get_settings(MoonlightSettings*); int moonlight_api_get_setting_value(MoonlightSettingKey,int*);
int moonlight_api_set_setting_value(MoonlightSettingKey,int); int moonlight_api_set_motion_scalar_x(float); int moonlight_api_set_motion_scalar_y(float);
int moonlight_api_save_settings(void); int moonlight_api_apply_settings(void); int moonlight_api_set_event_callback(MoonlightEventCallback,void*);
int moonlight_api_get_hosts(MoonlightHost*,int); int moonlight_api_search_hosts(void); int moonlight_api_add_host(const char*,uint16_t,const char*);
int moonlight_api_pair_host(const char*); int moonlight_api_start_stream(const char*); int moonlight_api_stop_stream(void);
#ifdef __cplusplus
}
#endif
#endif
