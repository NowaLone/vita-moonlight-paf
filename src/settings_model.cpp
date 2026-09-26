#ifdef __SNC__
#include <kernel.h>
#else
#include <psp2/kernel/clib.h>
#endif

#include <stdio.h>
#include "settings_model.h"

static SettingRow g_rows[kSettingRowMax];
static SettingSection g_sections[kSettingSectionMax];
static int g_row_count = 0;
static int g_section_count = 0;
static int g_ready = 0;

static const wchar_t *k_resolutions[] = {
    L"960x540", L"960x544", L"1024x576", L"1152x648", L"1280x540",
    L"1280x720", L"1366x768", L"1600x900", L"1920x1080"
};
static const wchar_t *k_fps[] = { L"24", L"30", L"40", L"50", L"60" };
static const wchar_t *k_bitrate[] = {
    L"2000", L"4000", L"5000", L"8000", L"10000", L"15000", L"20000", L"30000"
};
static const wchar_t *k_controller[] = { L"Xbox", L"PlayStation" };
static const wchar_t *k_touch[] = { L"Off", L"DS4 Touchpad", L"Mouse Absolute", L"Tablet (Sunshine)" };
static const wchar_t *k_keyboard[] = { L"US", L"DE", L"ES", L"FR", L"RU" };

static SettingRow make_category(const wchar_t *label) {
    SettingRow row = {};
    row.kind = KIND_CATEGORY;
    row.label = label;
    return row;
}

static SettingRow make_note(const wchar_t *label) {
    SettingRow row = {};
    row.kind = KIND_NOTE;
    row.label = label;
    return row;
}

static SettingRow make_fixed(const wchar_t *label, const wchar_t *value) {
    SettingRow row = {};
    row.kind = KIND_FIXED;
    row.label = label;
    row.fixed = value;
    return row;
}

static SettingRow make_toggle(const wchar_t *label, int value) {
    SettingRow row = {};
    row.kind = KIND_TOGGLE;
    row.label = label;
    row.value = value;
    return row;
}

static SettingRow make_choice(const wchar_t *label, const wchar_t *const *choices, int count, int value, int arrows) {
    SettingRow row = {};
    row.kind = KIND_CHOICE;
    row.label = label;
    row.choices = choices;
    row.choice_count = count;
    row.value = value;
    row.arrows = arrows;
    return row;
}

static SettingRow make_int(const wchar_t *label, int value, int step, int min_v, int max_v, int arrows) {
    SettingRow row = {};
    row.kind = KIND_INT;
    row.label = label;
    row.value = value;
    row.step = step;
    row.min_v = min_v;
    row.max_v = max_v;
    row.arrows = arrows;
    return row;
}

void settings_model_init() {
    if (g_ready) {
        return;
    }
    g_ready = 1;

    int i = 0;
    g_rows[i++] = make_category(L"Stream");
    g_rows[i++] = make_choice(L"Resolution", k_resolutions, 9, 1, 1);
    g_rows[i++] = make_choice(L"FPS", k_fps, 5, 4, 1);
    g_rows[i++] = make_choice(L"Bitrate", k_bitrate, 8, 4, 0);
    g_rows[i++] = make_toggle(L"Change graphical game settings for performance", 1);
    g_rows[i++] = make_toggle(L"Enable reference frame invalidation", 0);
    g_rows[i++] = make_toggle(L"Enable stream optimization", 1);
    g_rows[i++] = make_toggle(L"Enable VITA vblank", 0);
    g_rows[i++] = make_toggle(L"Enable frame pacer", 1);
    g_rows[i++] = make_toggle(L"Enable local audio", 0);

    g_rows[i++] = make_category(L"System");
    g_rows[i++] = make_toggle(L"Enable debug log", 1);
    g_rows[i++] = make_toggle(L"Disable power save", 1);
    g_rows[i++] = make_toggle(L"Swap X & O for Moonlight", 0);
    g_rows[i++] = make_toggle(L"Display streaming FPS", 0);

    g_rows[i++] = make_category(L"Input");
    g_rows[i++] = make_toggle(L"Enable Gyroscope reporting", 0);
    g_rows[i++] = make_toggle(L"Enable double tap to sprint", 0);
    g_rows[i++] = make_int(L"Sprint double tap time", 200, 50, 50, 500, 0);
    g_rows[i++] = make_choice(L"Controller type", k_controller, 2, 1, 1);
    g_rows[i++] = make_toggle(L"Swap R1/L1 <-> R2/L2", 0);
    g_rows[i++] = make_int(L"Mouse acceleration", 0, 15, 0, 300, 1);
    g_rows[i++] = make_toggle(L"Enable mapping file", 0);
    g_rows[i++] = make_note(L"Located at ux0:data/moonlight/vita.conf");
    g_rows[i++] = make_note(L"Example in github repo.");
    g_rows[i++] = make_toggle(L"Enable PS button capture", 0);
    g_rows[i++] = make_fixed(L"Back touchscreen deadzone", L"0px,0px,0px,0px");
    g_rows[i++] = make_toggle(L"Enable touchscreen special keys", 0);
    g_rows[i++] = make_fixed(L"Touchscreen special keys", L"...");
    g_rows[i++] = make_choice(L"Touchscreen mode", k_touch, 4, 0, 0);

    g_rows[i++] = make_category(L"Keyboard");
    g_rows[i++] = make_choice(L"Keyboard layout", k_keyboard, 5, 0, 0);
    g_row_count = i;

    g_section_count = 0;
    for (int row = 0; row < i; row++) {
        if (g_rows[row].kind != KIND_CATEGORY || g_section_count >= kSettingSectionMax) {
            continue;
        }
        if (g_section_count > 0) {
            SettingSection& prev = g_sections[g_section_count - 1];
            prev.count = row - prev.first;
        }
        g_sections[g_section_count].title = g_rows[row].label;
        g_sections[g_section_count].first = row + 1;
        g_sections[g_section_count].count = i - (row + 1);
        g_section_count++;
    }
}

int settings_model_row_count() {
    settings_model_init();
    return g_row_count;
}

SettingRow *settings_model_row(int index) {
    settings_model_init();
    if (index < 0 || index >= g_row_count) {
        return NULL;
    }
    return &g_rows[index];
}

int settings_model_section_count() {
    settings_model_init();
    return g_section_count;
}

SettingSection *settings_model_section(int index) {
    settings_model_init();
    if (index < 0 || index >= g_section_count) {
        return NULL;
    }
    return &g_sections[index];
}

static void ascii_to_wide(wchar_t *dst, int cap, const char *src) {
    int i = 0;
    if (cap <= 0) {
        return;
    }
    for (; src[i] != '\0' && i < cap - 1; i++) {
        dst[i] = (wchar_t)src[i];
    }
    dst[i] = 0;
}

void settings_model_format_value(const SettingRow& row, wchar_t *dst, int cap) {
    char buf[64];
    if (row.kind == KIND_TOGGLE) {
        ascii_to_wide(dst, cap, row.value ? "yes" : "no");
        return;
    }
    if (row.kind == KIND_CHOICE && row.choices != NULL && row.choice_count > 0) {
        int index = row.value;
        if (index < 0) {
            index = 0;
        }
        if (index >= row.choice_count) {
            index = row.choice_count - 1;
        }
        int n = 0;
        const wchar_t *src = row.choices[index];
        for (; src[n] != 0 && n < cap - 1; n++) {
            dst[n] = src[n];
        }
        dst[n] = 0;
        return;
    }
    if (row.kind == KIND_INT) {
        snprintf(buf, sizeof(buf), "%d", row.value);
        ascii_to_wide(dst, cap, buf);
        return;
    }
    if (row.kind == KIND_FIXED && row.fixed != NULL) {
        int n = 0;
        for (; row.fixed[n] != 0 && n < cap - 1; n++) {
            dst[n] = row.fixed[n];
        }
        dst[n] = 0;
        return;
    }
    if (cap > 0) {
        dst[0] = 0;
    }
}
