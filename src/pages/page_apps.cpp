#include <paf.h>
#include <string.h>

#include "pages/page_apps.h"
#include "app/moonlight_app.h"
#include "pages/page_stream.h"
#include "pages/page_main.h"
#include "pages/page_search.h"

namespace page {

namespace {
static const int kMaxApps = 8;
}

Apps::Apps()
    : Base("page_apps", "btn_close_apps",
           paf::Plugin::TransitionType_SlideFromBottom,
           paf::Plugin::TransitionType_SlideFromBottom),
      m_app_count(0),
      m_selected_index(-1),
      m_launching(false),
      m_list(NULL) {
    if (!IsValid()) return;

    for (int i = 0; i < kMaxApps; ++i) {
        memset(&m_apps[i], 0, sizeof(m_apps[i]));
        m_button_contexts[i].page = this;
        m_button_contexts[i].index = i;
    }

    m_list = static_cast<paf::ui::ListView *>(
        root->FindChild("list_view_generic"));

    if (m_list) {
        m_list->SetItemFactory(new ListViewFactory(this));
        m_list->InsertSegment(0, 1);
        m_list->SetCellSizeDefault(0, { 960.0f, 80.0f });
        m_list->SetSegmentLayoutType(0, paf::ui::ListView::LAYOUT_TYPE_LIST);
    }

    MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);

    paf::ui::Widget *title = root->FindChild("main_title_text");
    if (title) {
        ((paf::ui::Text *)title)->SetString(
            paf::common::string_util::ToWString("Applications"));
    }

    SetStatus("Loading applications...");

    int result = MoonlightApp::Instance()->Applications().Refresh();
    if (result != 0) {
        SetStatus("Unable to load applications.");
    }
}

Apps::~Apps() {
    if (!MoonlightApp::Instance() ||
        !MoonlightApp::Instance()->IsInitialized()) {
        return;
    }

    Search *search = static_cast<Search *>(Base::Find("page_search_pcs"));
    if (search) {
        search->RestoreEventCallback();
        return;
    }

    Main *main = Main::Instance();
    if (main) {
        main->RestoreEventCallback();
        return;
    }

    MoonlightApp::Instance()->SetEventCallback(NULL, NULL);
}

void Apps::RestoreEventCallback() {
    if (MoonlightApp::Instance()->IsInitialized()) {
        MoonlightApp::Instance()->SetEventCallback(OnMoonlightEvent, this);
    }
}

paf::ui::ListItem *Apps::CreateListItem(
    paf::ui::listview::ItemFactory::CreateParam &param) {
    paf::Plugin::TemplateOpenParam tmp;
    g_plugin->TemplateOpen(param.parent, "template_generic_list_item", tmp);

    paf::ui::ListItem *item = static_cast<paf::ui::ListItem *>(
        param.parent->GetChild(param.parent->GetChildrenNum() - 1));

    paf::ui::Widget *button = item->FindChild("image_button_list_item");
    if (!button || param.cell_index < 0 || param.cell_index >= m_app_count) {
        return item;
    }

    button->SetName((uint32_t)param.cell_index);
    button->AddEventCallback(
        paf::ui::ButtonBase::CB_BTN_DECIDE,
        OnAppButton,
        &m_button_contexts[param.cell_index]);

    const MoonlightApplication &app = m_apps[param.cell_index];
    button->SetString(
        paf::common::string_util::ToWString(
            app.name[0] ? app.name : "Application"));

    return item;
}

void Apps::OnAppButton(int32_t type,
                       paf::ui::Handler *self,
                       paf::ui::Event *event,
                       void *userdata) {
    (void)type;
    (void)self;
    (void)event;

    AppButtonContext *context = (AppButtonContext *)userdata;
    if (!context || !context->page) return;

    context->page->SelectApp(context->index);
}

void Apps::SelectApp(int index) {
    if (m_launching || index < 0 || index >= m_app_count) return;

    m_launching = true;
    m_selected_index = index;
    SetStatus("Launching application...");

    int result = MoonlightApp::Instance()->Connection().Start(
        m_apps[index].id);

    if (result != 0) {
        m_launching = false;
        paf::string status = paf::common::FormatString(
            "Unable to launch application: 0x%08X",
            (unsigned int)result);
        SetStatus(status.c_str());
        return;
    }

    SetStatus("Waiting for PC...");
}

void Apps::SetStatus(const char *text) {
    if (!root || !text) return;

    paf::ui::Widget *widget = root->FindChild("text_apps_status");
    if (!widget) return;

    ((paf::ui::Text *)widget)->SetString(
        paf::common::string_util::ToWString(text));
}

void Apps::ShowApps() {
    if (!root || !m_list) return;

    m_app_count = MoonlightApp::Instance()->Applications().GetAll(
        m_apps,
        kMaxApps);

    if (m_app_count < 0) m_app_count = 0;
    if (m_app_count > kMaxApps) m_app_count = kMaxApps;

    const int existing = m_list->GetCellNum(0);
    if (existing > 0) {
        m_list->DeleteCell(0, 0, existing);
    }

    if (m_app_count > 0) {
        m_list->InsertCell(0, 0, m_app_count);
        SetStatus("Select an application.");
    } else {
        SetStatus("No applications available.");
    }
}

void Apps::OnMoonlightEvent(
    const MoonlightEvent *event,
    void *userdata) {
    Apps *apps = (Apps *)userdata;
    if (!apps || !event) return;

    switch (event->type) {
    case MOONLIGHT_EVENT_APPLICATIONS_READY:
        apps->ShowApps();
        break;

    case MOONLIGHT_EVENT_APPLICATIONS_FAILED: {
        paf::string status = paf::common::FormatString(
            "Unable to load applications: 0x%08X",
            (unsigned int)event->result);
        apps->SetStatus(status.c_str());
        break;
    }

    case MOONLIGHT_EVENT_STREAM_STARTED: {
        if (!Base::IsOpen("page_stream")) {
            Stream *stream = new Stream();
            if (!stream->IsValid()) {
                delete stream;
            }
        }

        apps->SetStatus("Connected.");
        break;
    }

    case MOONLIGHT_EVENT_STREAM_STOPPED:
        if (Base::IsOpen("page_stream")) {
            Base *stream = Base::Find("page_stream");
            delete stream;
        }

        Stream::ScheduleFrameBufferRelease();
        apps->m_launching = false;
        apps->SetStatus("Stream stopped.");
        break;

    case MOONLIGHT_EVENT_STREAM_FAILED: {
        paf::string status = paf::common::FormatString(
            "Unable to start stream: 0x%08X",
            (unsigned int)event->result);
        apps->m_launching = false;
        apps->SetStatus(status.c_str());
        break;
    }

    default:
        break;
    }
}

}
