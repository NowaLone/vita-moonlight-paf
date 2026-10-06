#ifndef VITA_MOONLIGHT_TYPES_H
#define VITA_MOONLIGHT_TYPES_H

#include <stdint.h>

typedef enum MoonlightSettingKey {
    MOONLIGHT_SETTING_RESOLUTION = 0,
    MOONLIGHT_SETTING_FPS,
    MOONLIGHT_SETTING_BITRATE,
    MOONLIGHT_SETTING_SOPS,
    MOONLIGHT_SETTING_REF_FRAME_INVALIDATION,
    MOONLIGHT_SETTING_STREAM_OPTIMIZATION,
    MOONLIGHT_SETTING_VITA_VBLANK,
    MOONLIGHT_SETTING_FRAME_PACER,
    MOONLIGHT_SETTING_LOCAL_AUDIO,
    MOONLIGHT_SETTING_DEBUG_LOG,
    MOONLIGHT_SETTING_DISABLE_POWER_SAVE,
    MOONLIGHT_SETTING_SWAP_XO,
    MOONLIGHT_SETTING_SHOW_FPS,
    MOONLIGHT_SETTING_GYRO,
    MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT,
    MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME,
    MOONLIGHT_SETTING_CONTROLLER_TYPE,
    MOONLIGHT_SETTING_SWAP_SHOULDERS,
    MOONLIGHT_SETTING_MOUSE_ACCELERATION,
    MOONLIGHT_SETTING_MAPPING_ENABLED,
    MOONLIGHT_SETTING_PS_BUTTON_CAPTURE,
    MOONLIGHT_SETTING_FRONT_TOUCHZONES,
    MOONLIGHT_SETTING_TOUCHSCREEN_MODE,
    MOONLIGHT_SETTING_KEYBOARD_LAYOUT,
    MOONLIGHT_SETTING_BACK_DEADZONE_TOP,
    MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT,
    MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM,
    MOONLIGHT_SETTING_BACK_DEADZONE_LEFT,
    MOONLIGHT_SETTING_CENTER_REGION_ONLY,
    MOONLIGHT_SETTING_COUNT
} MoonlightSettingKey;

typedef struct MoonlightSettings {
    int width;
    int height;
    int fps;
    int bitrate;
    int sops;
    int localaudio;
    int enable_frame_pacer;
    int center_region_only;
    int disable_powersave;
    int jp_layout;
    int show_fps;
    int save_debug_log;
    int enable_front_touchzones;
    int mapping_enabled;
    int mouse_acceleration;
    int enable_ref_frame_invalidation;
    int enable_remote_stream_optimization;
    int enable_vita_vblank_wait;
    int enable_motion_controls;
    int enable_psbutton_capture;
    int enable_double_tap_sprint;
    int double_tap_sprint_step_time;
    int keyboard_layout;
    int touchscreen_mode;
    int controller_type;
    int swap_shoulder_buttons;
    int back_deadzone_top;
    int back_deadzone_right;
    int back_deadzone_bottom;
    int back_deadzone_left;
} MoonlightSettings;

typedef struct MoonlightHost {
    int id;
    char host_id[33];
    char name[256];
    char internal[256];
    char external[256];
    char mac[18];
    uint16_t port;
    int paired;
    int online;
    int prefer_external;
} MoonlightHost;

typedef struct MoonlightApplication {
    int id;
    char name[256];
} MoonlightApplication;

typedef enum MoonlightConnectionState {
    MOONLIGHT_CONNECTION_DISCONNECTED = 0,
    MOONLIGHT_CONNECTION_READY,
    MOONLIGHT_CONNECTION_PAIRED,
    MOONLIGHT_CONNECTION_STREAMING,
    MOONLIGHT_CONNECTION_PAUSED
} MoonlightConnectionState;

typedef enum MoonlightEventType {
    MOONLIGHT_EVENT_NONE = 0,
    MOONLIGHT_EVENT_HOST_SCAN_STARTED,
    MOONLIGHT_EVENT_HOST_SCAN_FINISHED,
    MOONLIGHT_EVENT_HOST_SCAN_FAILED,
    MOONLIGHT_EVENT_HOSTS_CHANGED,
    MOONLIGHT_EVENT_PAIRING_REQUIRED,
    MOONLIGHT_EVENT_PAIRING_FINISHED,
    MOONLIGHT_EVENT_PAIRING_FAILED,
    MOONLIGHT_EVENT_CONNECTION_STARTED,
    MOONLIGHT_EVENT_CONNECTION_READY,
    MOONLIGHT_EVENT_CONNECTION_FAILED,
    MOONLIGHT_EVENT_CONNECTION_CLOSED,
    MOONLIGHT_EVENT_STREAM_STARTED,
    MOONLIGHT_EVENT_STREAM_STOPPED,
    MOONLIGHT_EVENT_STREAM_FAILED,
    MOONLIGHT_EVENT_APPLICATIONS_READY,
    MOONLIGHT_EVENT_APPLICATIONS_FAILED,
    MOONLIGHT_EVENT_SETTINGS_CHANGED,
    MOONLIGHT_EVENT_SETTINGS_CLOSED
} MoonlightEventType;

typedef struct MoonlightEvent {
    MoonlightEventType type;
    int result;
    int host_id;
    int application_id;
    const char *address;
    char pairing_pin[5];
} MoonlightEvent;

typedef void (*MoonlightEventCallback)(const MoonlightEvent *, void *);

#endif
