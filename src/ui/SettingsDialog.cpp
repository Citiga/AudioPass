#include "ui/SettingsDialog.h"

#include "Theme.h"
#include "ui/Widgets.h"

#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>

#include <windows.h>
#include <winreg.h>

namespace bv {
namespace {

wxStaticText* Caption(wxWindow* parent, const wxString& text, const wxFont& font, const wxColour& colour) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetFont(font);
    label->SetForegroundColour(colour);
    label->SetBackgroundColour(theme::Bg());
    return label;
}

CheckBox* Option(wxWindow* parent, const wxString& label, const wxString& tip, bool value, std::function<void(bool)> changed) {
    auto* box = new CheckBox(parent, label);
    box->SetToolTip(tip);
    box->SetValue(value);
    box->onChange = std::move(changed);
    return box;
}

} // namespace

bool enableStartWithWindows(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    LSTATUS result = ERROR_SUCCESS;
    if (!enabled) {
        result = RegDeleteValueW(key, L"AudioPass");
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    } else {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) {
            RegCloseKey(key);
            return false;
        }
        path.resize(length);
        const std::wstring command = L"\"" + path + L"\"";
        result = RegSetValueExW(key, L"AudioPass", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                 static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

SettingsDialog::SettingsDialog(wxWindow* parent, Session& session, std::function<void()> apply)
    : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX) {
    SetBackgroundColour(theme::Bg());
    SetForegroundColour(theme::Text());
    SetFont(theme::Ui(10));

    auto* title = Caption(this, "Settings", theme::Ui(18, true), theme::Text());
    auto* tray = Option(this, "Minimize to the system tray",
                        "The minimize button hides the window. Open it again from the tray icon.", session.minimizeToTray,
                        [&session, apply](bool on) {
                            session.minimizeToTray = on;
                            apply();
                        });
    auto* closeMinimizes = Option(this, "Minimize instead of quitting",
                                  "The close button hides AudioPass instead of quitting.", session.closeMinimizes,
                                  [&session, apply](bool on) {
                                      session.closeMinimizes = on;
                                      apply();
                                  });
    auto* windows = Option(this, "Start with Windows", "Open AudioPass when you sign in to Windows.",
                           session.startWithWindows, {});
    auto* activate = Option(this, "Start audio when AudioPass opens",
                            "Press Start automatically after the plugin chain is restored.", session.activateOnStart,
                            [&session, apply](bool on) {
                                session.activateOnStart = on;
                                apply();
                            });
    auto* hint = Caption(this, "Right-click the tray icon and choose Exit to quit.", theme::Ui(8), theme::Faint());
    windows->onChange = [&session, apply, windows, hint](bool on) {
        if (!enableStartWithWindows(on)) {
            windows->SetValue(session.startWithWindows);
            hint->SetForegroundColour(theme::Danger());
            hint->SetLabel("Could not update the Windows startup entry.");
            return;
        }
        hint->SetForegroundColour(theme::Faint());
        hint->SetLabel("Right-click the tray icon and choose Exit to quit.");
        session.startWithWindows = on;
        apply();
    };
    auto* done = new FlatButton(this, "Close", FlatButton::Kind::Primary);
    done->onClick = [this] { EndModal(wxID_CLOSE); };

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(22));
    sizer->Add(tray, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(18));
    sizer->Add(closeMinimizes, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->Add(windows, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->Add(activate, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    sizer->Add(done, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(20));
    SetSizer(sizer);
    sizer->Fit(this);
    CentreOnParent();
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
}

} // namespace bv
