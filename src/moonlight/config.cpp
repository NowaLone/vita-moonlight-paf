#include "moonlight/config.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
static const int LINE_MAX_LEN = 1024;
static const int PATH_MAX_LEN = 512;

static MoonlightSettings g_settings;
static char g_config_path[PATH_MAX_LEN];
static int g_initialized = 0;
static int g_dirty = 0;

static void defaults() {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.width = 1280;
    g_settings.height = 720;
    g_settings.fps = 60;
    g_settings.bitrate = 5000;
    g_settings.sops = 1;
    g_settings.enable_frame_pacer = 1;
    g_settings.disable_powersave = 1;
    g_settings.mouse_acceleration = 150;
    g_settings.enable_psbutton_capture = 1;
    g_settings.double_tap_sprint_step_time = 200;
    g_settings.motion_controls_scalar_x = 1.2f;
    g_settings.motion_controls_scalar_y = 0.8f;
    g_settings.controller_type = 2;
}

static int dir_exists(const char *path) {
    SceUID d = sceIoDopen(path);
    if (d < 0) return 0;
    sceIoDclose(d);
    return 1;
}

static int ensure_dir(const char *path) {
    int r = sceIoMkdir(path, 0777);
    return (r >= 0 || dir_exists(path)) ? 0 : r;
}

static void select_path() {
    static const char *dirs[] = {
        "ux0:data/moonlight",
        "ux0:moonlight",
        "uma0:data/moonlight"
    };

    /* Prefer an existing legacy config before creating a new location. */
    for (unsigned int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/moonlight.conf", dirs[i]);
        FILE *f = fopen(path, "r");
        if (f != NULL) {
            fclose(f);
            strncpy(g_config_path, path, sizeof(g_config_path) - 1);
            g_config_path[sizeof(g_config_path) - 1] = '\0';
            return;
        }
    }

    for (unsigned int i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (ensure_dir(dirs[i]) == 0) {
            snprintf(g_config_path, sizeof(g_config_path), "%s/moonlight.conf", dirs[i]);
            return;
        }
    }

    snprintf(g_config_path, sizeof(g_config_path), "ux0:data/moonlight.conf");
}

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);

    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
    *e = '\0';
}

static int as_bool(const char *s) {
    return !strcmp(s, "true") || !strcmp(s, "1");
}

static void set_key(const char *section, const char *key, const char *value) {
    if (!strcmp(section, "backtouchscreen_deadzone")) {
        if (!strcmp(key, "top")) g_settings.back_deadzone_top = atoi(value);
        else if (!strcmp(key, "right")) g_settings.back_deadzone_right = atoi(value);
        else if (!strcmp(key, "bottom")) g_settings.back_deadzone_bottom = atoi(value);
        else if (!strcmp(key, "left")) g_settings.back_deadzone_left = atoi(value);
        return;
    }

    if (section[0] != '\0') return;

    if (!strcmp(key, "width")) g_settings.width = atoi(value);
    else if (!strcmp(key, "height")) g_settings.height = atoi(value);
    else if (!strcmp(key, "fps")) g_settings.fps = atoi(value);
    else if (!strcmp(key, "bitrate")) g_settings.bitrate = atoi(value);
    else if (!strcmp(key, "sops")) g_settings.sops = as_bool(value);
    else if (!strcmp(key, "localaudio")) g_settings.localaudio = as_bool(value);
    else if (!strcmp(key, "enable_frame_pacer")) g_settings.enable_frame_pacer = as_bool(value);
    else if (!strcmp(key, "center_region_only")) g_settings.center_region_only = as_bool(value);
    else if (!strcmp(key, "disable_powersave")) g_settings.disable_powersave = as_bool(value);
    else if (!strcmp(key, "jp_layout")) g_settings.jp_layout = as_bool(value);
    else if (!strcmp(key, "show_fps")) g_settings.show_fps = as_bool(value);
    else if (!strcmp(key, "save_debug_log")) g_settings.save_debug_log = as_bool(value);
    else if (!strcmp(key, "enable_front_touchzones")) g_settings.enable_front_touchzones = as_bool(value);
    else if (!strcmp(key, "mouse_acceleration")) g_settings.mouse_acceleration = atoi(value);
    else if (!strcmp(key, "enable_ref_frame_invalidation")) g_settings.enable_ref_frame_invalidation = as_bool(value);
    else if (!strcmp(key, "enable_remote_stream_optimization")) g_settings.enable_remote_stream_optimization = atoi(value);
    else if (!strcmp(key, "enable_vita_vblank_wait")) g_settings.enable_vita_vblank_wait = as_bool(value);
    else if (!strcmp(key, "enable_motion_controls")) g_settings.enable_motion_controls = as_bool(value);
    else if (!strcmp(key, "enable_psbutton_capture")) g_settings.enable_psbutton_capture = as_bool(value);
    else if (!strcmp(key, "enable_double_tap_sprint")) g_settings.enable_double_tap_sprint = as_bool(value);
    else if (!strcmp(key, "double_tap_sprint_step_time")) g_settings.double_tap_sprint_step_time = atoi(value);
    else if (!strcmp(key, "motion_controls_scalar_x")) g_settings.motion_controls_scalar_x = (float)atof(value);
    else if (!strcmp(key, "motion_controls_scalar_y")) g_settings.motion_controls_scalar_y = (float)atof(value);
    else if (!strcmp(key, "keyboard_layout")) g_settings.keyboard_layout = atoi(value);
    else if (!strcmp(key, "touchscreen_mode")) g_settings.touchscreen_mode = atoi(value);
    else if (!strcmp(key, "controller_type")) g_settings.controller_type = atoi(value);
    else if (!strcmp(key, "swap_shoulder_buttons")) g_settings.swap_shoulder_buttons = as_bool(value);
}

static void load_file(FILE *file) {
    char line[LINE_MAX_LEN];
    char section[128] = "";

    while (fgets(line, sizeof(line), file) != NULL) {
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            char *end = strchr(line + 1, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char key[LINE_MAX_LEN];
        char value[LINE_MAX_LEN];
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        strncpy(value, eq + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        trim(key);
        trim(value);
        set_key(section, key, value);
    }
}

struct SaveKey { const char *name; char value[64]; int found; };

static int build_keys(SaveKey *keys) {
    static const char *names[] = {
        "width","height","fps","bitrate","sops","localaudio","enable_frame_pacer",
        "center_region_only","disable_powersave","jp_layout","show_fps","save_debug_log",
        "enable_front_touchzones","mouse_acceleration","enable_ref_frame_invalidation",
        "enable_remote_stream_optimization","enable_vita_vblank_wait","enable_motion_controls",
        "enable_psbutton_capture","enable_double_tap_sprint","double_tap_sprint_step_time",
        "motion_controls_scalar_x","motion_controls_scalar_y","keyboard_layout",
        "touchscreen_mode","swap_shoulder_buttons","controller_type"
    };
    for (int i = 0; i < 27; ++i) {
        keys[i].name = names[i];
        keys[i].found = 0;
    }

    snprintf(keys[0].value,64,"%d",g_settings.width);
    snprintf(keys[1].value,64,"%d",g_settings.height);
    snprintf(keys[2].value,64,"%d",g_settings.fps);
    snprintf(keys[3].value,64,"%d",g_settings.bitrate);
    snprintf(keys[4].value,64,"%s",g_settings.sops?"true":"false");
    snprintf(keys[5].value,64,"%s",g_settings.localaudio?"true":"false");
    snprintf(keys[6].value,64,"%s",g_settings.enable_frame_pacer?"true":"false");
    snprintf(keys[7].value,64,"%s",g_settings.center_region_only?"true":"false");
    snprintf(keys[8].value,64,"%s",g_settings.disable_powersave?"true":"false");
    snprintf(keys[9].value,64,"%s",g_settings.jp_layout?"true":"false");
    snprintf(keys[10].value,64,"%s",g_settings.show_fps?"true":"false");
    snprintf(keys[11].value,64,"%s",g_settings.save_debug_log?"true":"false");
    snprintf(keys[12].value,64,"%s",g_settings.enable_front_touchzones?"true":"false");
    snprintf(keys[13].value,64,"%d",g_settings.mouse_acceleration);
    snprintf(keys[14].value,64,"%s",g_settings.enable_ref_frame_invalidation?"true":"false");
    snprintf(keys[15].value,64,"%d",g_settings.enable_remote_stream_optimization);
    snprintf(keys[16].value,64,"%s",g_settings.enable_vita_vblank_wait?"true":"false");
    snprintf(keys[17].value,64,"%s",g_settings.enable_motion_controls?"true":"false");
    snprintf(keys[18].value,64,"%s",g_settings.enable_psbutton_capture?"true":"false");
    snprintf(keys[19].value,64,"%s",g_settings.enable_double_tap_sprint?"true":"false");
    snprintf(keys[20].value,64,"%d",g_settings.double_tap_sprint_step_time);
    snprintf(keys[21].value,64,"%.6f",(double)g_settings.motion_controls_scalar_x);
    snprintf(keys[22].value,64,"%.6f",(double)g_settings.motion_controls_scalar_y);
    snprintf(keys[23].value,64,"%d",g_settings.keyboard_layout);
    snprintf(keys[24].value,64,"%d",g_settings.touchscreen_mode);
    snprintf(keys[25].value,64,"%s",g_settings.swap_shoulder_buttons?"true":"false");
    snprintf(keys[26].value,64,"%d",g_settings.controller_type);
    return 27;
}

static SaveKey *find_key(SaveKey *keys, int count, const char *name) {
    for (int i = 0; i < count; ++i) if (!strcmp(keys[i].name, name)) return &keys[i];
    return NULL;
}

static void write_missing(FILE *out, SaveKey *keys, int count) {
    for (int i = 0; i < count; ++i) if (!keys[i].found) {
        fprintf(out, "%s = %s\n", keys[i].name, keys[i].value);
        keys[i].found = 1;
    }
}

static int preserve(FILE *in, FILE *out) {
    char line[LINE_MAX_LEN], section[128] = "";
    SaveKey keys[32];
    int count = build_keys(keys);

    while (fgets(line, sizeof(line), in) != NULL) {
        char original[LINE_MAX_LEN];
        char parsed[LINE_MAX_LEN];
        strncpy(original, line, sizeof(original) - 1);
        original[sizeof(original) - 1] = '\0';
        strncpy(parsed, line, sizeof(parsed) - 1);
        parsed[sizeof(parsed) - 1] = '\0';
        trim(parsed);

        if (parsed[0] == '[') {
            if (section[0] == '\0') write_missing(out, keys, count);
            char *end = strchr(parsed + 1, ']');
            if (end) {
                *end = '\0';
                strncpy(section, parsed + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            fputs(original, out);
            continue;
        }

        if (section[0] == '\0' && parsed[0] && parsed[0] != '#' && parsed[0] != ';') {
            char *eq = strchr(parsed, '=');
            if (eq) {
                *eq = '\0';
                trim(parsed);
                SaveKey *key = find_key(keys, count, parsed);
                if (key) {
                    fprintf(out, "%s = %s\n", key->name, key->value);
                    key->found = 1;
                    continue;
                }
            }
        }

        fputs(original, out);
    }

    if (section[0] == '\0') write_missing(out, keys, count);
    return ferror(out) ? -1 : 0;
}

static int write_new(FILE *out) {
    SaveKey keys[32];
    int count = build_keys(keys);
    const char *slash = strrchr(g_config_path, '/');
    char dir[PATH_MAX_LEN] = "ux0:data";
    if (slash) {
        size_t n = (size_t)(slash - g_config_path);
        if (n >= sizeof(dir)) n = sizeof(dir) - 1;
        memcpy(dir, g_config_path, n);
        dir[n] = '\0';
    }
    fprintf(out, "key_dir = %s/\n\n", dir);
    for (int i = 0; i < count; ++i) fprintf(out, "%s = %s\n", keys[i].name, keys[i].value);
    return ferror(out) ? -1 : 0;
}

static int supported(MoonlightSettingKey key) {
    return key >= MOONLIGHT_SETTING_RESOLUTION && key < MOONLIGHT_SETTING_COUNT;
}

static int resolution_get() {
    static const int r[][2] = {{960,540},{960,544},{1024,576},{1152,648},{1280,540},{1280,720},{1366,768},{1600,900},{1920,1080}};
    for (int i=0;i<9;++i) if (g_settings.width==r[i][0] && g_settings.height==r[i][1]) return i;
    return 5;
}

static int resolution_set(int i) {
    static const int r[][2] = {{960,540},{960,544},{1024,576},{1152,648},{1280,540},{1280,720},{1366,768},{1600,900},{1920,1080}};
    if (i < 0 || i >= 9) return -1;
    g_settings.width=r[i][0]; g_settings.height=r[i][1]; g_dirty=1; return 0;
}
}

extern "C" {
int moonlight_config_init(void) {
    if (g_initialized) return 0;
    defaults();
    g_dirty = 0;
    select_path();
    FILE *f = fopen(g_config_path, "r");
    if (f) { load_file(f); fclose(f); } else g_dirty = 1;

    if (g_settings.fps <= 0) g_settings.fps = g_settings.height >= 1080 ? 30 : 60;
    if (g_settings.bitrate <= 0)
        g_settings.bitrate = (g_settings.height >= 1080 && g_settings.fps >= 60) ? 20000 :
                              ((g_settings.height >= 1080 || g_settings.fps >= 60) ? 10000 : 5000);
    g_initialized = 1;
    return 0;
}

void moonlight_config_shutdown(void) {
    if (!g_initialized) return;
    if (g_dirty) moonlight_config_save();
    g_initialized = 0;
}

const char *moonlight_config_path(void) { return g_config_path; }

int moonlight_config_get(MoonlightSettings *out) {
    if (!g_initialized || !out) return -1;
    *out = g_settings;
    return 0;
}

int moonlight_config_get_value(MoonlightSettingKey k, int *out) {
    if (!g_initialized || !out || !supported(k)) return -1;
    switch (k) {
    case MOONLIGHT_SETTING_RESOLUTION: *out=resolution_get(); return 0;
    case MOONLIGHT_SETTING_FPS: *out=g_settings.fps; break;
    case MOONLIGHT_SETTING_BITRATE: *out=g_settings.bitrate; break;
    case MOONLIGHT_SETTING_SOPS: *out=g_settings.sops; break;
    case MOONLIGHT_SETTING_REF_FRAME_INVALIDATION: *out=g_settings.enable_ref_frame_invalidation; break;
    case MOONLIGHT_SETTING_STREAM_OPTIMIZATION: *out=g_settings.enable_remote_stream_optimization; break;
    case MOONLIGHT_SETTING_VITA_VBLANK: *out=g_settings.enable_vita_vblank_wait; break;
    case MOONLIGHT_SETTING_FRAME_PACER: *out=g_settings.enable_frame_pacer; break;
    case MOONLIGHT_SETTING_LOCAL_AUDIO: *out=g_settings.localaudio; break;
    case MOONLIGHT_SETTING_DEBUG_LOG: *out=g_settings.save_debug_log; break;
    case MOONLIGHT_SETTING_DISABLE_POWER_SAVE: *out=g_settings.disable_powersave; break;
    case MOONLIGHT_SETTING_SWAP_XO: *out=g_settings.jp_layout; break;
    case MOONLIGHT_SETTING_SHOW_FPS: *out=g_settings.show_fps; break;
    case MOONLIGHT_SETTING_GYRO: *out=g_settings.enable_motion_controls; break;
    case MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT: *out=g_settings.enable_double_tap_sprint; break;
    case MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME: *out=g_settings.double_tap_sprint_step_time; break;
    case MOONLIGHT_SETTING_CONTROLLER_TYPE: *out=g_settings.controller_type-1; if(*out<0||*out>3)*out=1; break;
    case MOONLIGHT_SETTING_SWAP_SHOULDERS: *out=g_settings.swap_shoulder_buttons; break;
    case MOONLIGHT_SETTING_MOUSE_ACCELERATION: *out=g_settings.mouse_acceleration; break;
    case MOONLIGHT_SETTING_MAPPING_ENABLED: *out=0; break;
    case MOONLIGHT_SETTING_PS_BUTTON_CAPTURE: *out=g_settings.enable_psbutton_capture; break;
    case MOONLIGHT_SETTING_FRONT_TOUCHZONES: *out=g_settings.enable_front_touchzones; break;
    case MOONLIGHT_SETTING_TOUCHSCREEN_MODE: *out=g_settings.touchscreen_mode; break;
    case MOONLIGHT_SETTING_KEYBOARD_LAYOUT: *out=g_settings.keyboard_layout; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_TOP: *out=g_settings.back_deadzone_top; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT: *out=g_settings.back_deadzone_right; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM: *out=g_settings.back_deadzone_bottom; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_LEFT: *out=g_settings.back_deadzone_left; break;
    case MOONLIGHT_SETTING_CENTER_REGION_ONLY: *out=g_settings.center_region_only; break;
    default: return -1;
    }
    return 0;
}

int moonlight_config_set_value(MoonlightSettingKey k, int v) {
    if (!g_initialized || !supported(k)) return -1;
    switch (k) {
    case MOONLIGHT_SETTING_RESOLUTION: return resolution_set(v);
    case MOONLIGHT_SETTING_FPS: g_settings.fps=v; break;
    case MOONLIGHT_SETTING_BITRATE: g_settings.bitrate=v; break;
    case MOONLIGHT_SETTING_SOPS: g_settings.sops=!!v; break;
    case MOONLIGHT_SETTING_REF_FRAME_INVALIDATION: g_settings.enable_ref_frame_invalidation=!!v; break;
    case MOONLIGHT_SETTING_STREAM_OPTIMIZATION: g_settings.enable_remote_stream_optimization=v; break;
    case MOONLIGHT_SETTING_VITA_VBLANK: g_settings.enable_vita_vblank_wait=!!v; break;
    case MOONLIGHT_SETTING_FRAME_PACER: g_settings.enable_frame_pacer=!!v; break;
    case MOONLIGHT_SETTING_LOCAL_AUDIO: g_settings.localaudio=!!v; break;
    case MOONLIGHT_SETTING_DEBUG_LOG: g_settings.save_debug_log=!!v; break;
    case MOONLIGHT_SETTING_DISABLE_POWER_SAVE: g_settings.disable_powersave=!!v; break;
    case MOONLIGHT_SETTING_SWAP_XO: g_settings.jp_layout=!!v; break;
    case MOONLIGHT_SETTING_SHOW_FPS: g_settings.show_fps=!!v; break;
    case MOONLIGHT_SETTING_GYRO: g_settings.enable_motion_controls=!!v; break;
    case MOONLIGHT_SETTING_DOUBLE_TAP_SPRINT: g_settings.enable_double_tap_sprint=!!v; break;
    case MOONLIGHT_SETTING_SPRINT_DOUBLE_TAP_TIME: g_settings.double_tap_sprint_step_time=v; break;
    case MOONLIGHT_SETTING_CONTROLLER_TYPE: if(v<0||v>3)return -1;g_settings.controller_type=v+1;break;
    case MOONLIGHT_SETTING_SWAP_SHOULDERS: g_settings.swap_shoulder_buttons=!!v; break;
    case MOONLIGHT_SETTING_MOUSE_ACCELERATION: g_settings.mouse_acceleration=v; break;
    case MOONLIGHT_SETTING_MAPPING_ENABLED: return 0;
    case MOONLIGHT_SETTING_PS_BUTTON_CAPTURE: g_settings.enable_psbutton_capture=!!v; break;
    case MOONLIGHT_SETTING_FRONT_TOUCHZONES: g_settings.enable_front_touchzones=!!v; break;
    case MOONLIGHT_SETTING_TOUCHSCREEN_MODE: g_settings.touchscreen_mode=v; break;
    case MOONLIGHT_SETTING_KEYBOARD_LAYOUT: g_settings.keyboard_layout=v; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_TOP: g_settings.back_deadzone_top=v; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_RIGHT: g_settings.back_deadzone_right=v; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_BOTTOM: g_settings.back_deadzone_bottom=v; break;
    case MOONLIGHT_SETTING_BACK_DEADZONE_LEFT: g_settings.back_deadzone_left=v; break;
    case MOONLIGHT_SETTING_CENTER_REGION_ONLY: g_settings.center_region_only=!!v; break;
    default:return -1;
    }
    g_dirty=1; return 0;
}

int moonlight_config_set_motion_scalar_x(float v){if(!g_initialized)return -1;g_settings.motion_controls_scalar_x=v;g_dirty=1;return 0;}
int moonlight_config_set_motion_scalar_y(float v){if(!g_initialized)return -1;g_settings.motion_controls_scalar_y=v;g_dirty=1;return 0;}

int moonlight_config_save(void) {
    if (!g_initialized) return -1;
    char tmp[PATH_MAX_LEN];
    snprintf(tmp,sizeof(tmp),"%s.tmp",g_config_path);
    FILE *in=fopen(g_config_path,"r"), *out=fopen(tmp,"w");
    if(!out){if(in)fclose(in);return -1;}
    int r=in?preserve(in,out):write_new(out);
    if(in)fclose(in);
    if(fclose(out)!=0)r=-1;
    if(r==0){sceIoRemove(g_config_path);r=sceIoRename(tmp,g_config_path);}
    else sceIoRemove(tmp);
    if(r==0)g_dirty=0;
    return r;
}
}
