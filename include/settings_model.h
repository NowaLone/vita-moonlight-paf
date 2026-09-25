#ifndef VITA_MOONLIGHT_SETTINGS_MODEL_H
#define VITA_MOONLIGHT_SETTINGS_MODEL_H

enum SettingKind {
    KIND_CATEGORY = 0,
    KIND_NOTE,
    KIND_FIXED,
    KIND_TOGGLE,
    KIND_CHOICE,
    KIND_INT
};

struct SettingRow {
    SettingKind kind;
    const wchar_t *label;
    const wchar_t *fixed;
    const wchar_t *const *choices;
    int choice_count;
    int value;
    int step;
    int min_v;
    int max_v;
    int arrows;
};

struct SettingSection {
    const wchar_t *title;
    int first;
    int count;
};

enum {
    kSettingRowMax = 32,
    kSettingSectionMax = 8
};

void settings_model_init();
int settings_model_row_count();
SettingRow *settings_model_row(int index);
int settings_model_section_count();
SettingSection *settings_model_section(int index);
void settings_model_format_value(const SettingRow& row, wchar_t *dst, int cap);

#endif
