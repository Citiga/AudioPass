#include "audio/Engine.h"
#include "ui/MainFrame.h"

#include <wx/wx.h>

#include <cstdio>

namespace {

void AttachOutput() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

class AudioPassApp : public wxApp {
public:
    bool OnInit() override;
};

bool AudioPassApp::OnInit() {
    SetAppName("AudioPass");
    if (argc > 1 && wxString(argv[1]) == "--list-devices") {
        AttachOutput();
        for (const auto& device : bv::enumerateDevices()) {
            std::wprintf(L"%s%s%s%s\n", device.capture ? L"IN  " : L"OUT ", device.name.c_str(),
                         device.virtualDevice ? L"  [virtual]" : L"",
                         device.isDefault ? L"  [default]" : L"");
        }
        return false;
    }
    auto* frame = new bv::MainFrame();
    frame->Show();
    return true;
}

} // namespace

wxIMPLEMENT_APP(AudioPassApp);
