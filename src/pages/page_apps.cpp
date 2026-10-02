    int result = MoonlightApp::Instance()->Connection().Start(m_apps[index].id);
    if (result != 0) {
        m_launching = false;
        SetStatus("Launch start failed");
        if (button) {
            button->SetString(paf::common::string_util::ToWString("LAUNCH FAILED"));
        }
        return;
    }

    if (button) {
        button->SetString(paf::common::string_util::ToWString("NO STREAM YET"));
    }
    SetStatus("Launch accepted, stream not implemented");
}