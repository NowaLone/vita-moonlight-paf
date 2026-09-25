#ifndef VITA_MOONLIGHT_PAGE_SETTINGS_H
#define VITA_MOONLIGHT_PAGE_SETTINGS_H

#include "pages/page.h"

namespace page {

class SettingsRoot : public Base {
public:
    SettingsRoot();
    virtual ~SettingsRoot();
    virtual Type GetType() { return Type_SettingsRoot; }

    static SettingsRoot *Instance();
};

class SettingsSection : public Base {
public:
    explicit SettingsSection(int section_index);
    virtual ~SettingsSection();
    virtual Type GetType() { return Type_SettingsSection; }

    int SectionIndex() const { return m_section_index; }

private:
    int m_section_index;
};

class ChoicePicker : public Base {
public:
    explicit ChoicePicker(int row_index);
    virtual ~ChoicePicker();
    virtual Type GetType() { return Type_ChoicePicker; }

    int RowIndex() const { return m_row_index; }

private:
    int m_row_index;
};

void SettingsPadInit();
void CloseSettingsSection();
void CloseSettingsRoot();
void CloseChoicePicker(int from_circle = 0);
void OpenChoicePicker(int row_index);
void SetSettingsBackActive(bool on);
void SetSettingsMenuFocusable(bool on);
void SetSectionRowsFocusable(bool on);

}

#endif
