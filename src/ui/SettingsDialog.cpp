#include "ui/SettingsDialog.h"

#include "Theme.h"
#include "ui/Widgets.h"

#include <string>

#include <wx/dirdlg.h>
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

wxString FolderText(const std::wstring& path) {
    if (path.empty()) {
        return "No folder selected";
    }
    wxString text(path);
    if (text.length() > 42) {
        text = "..." + text.Right(39);
    }
    return text;
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
    auto* logCaption = Caption(this, "Level history folder", theme::Ui(8, true), theme::Muted());
    auto* logPath = new wxStaticText(this, wxID_ANY, FolderText(session.levelLogDir));
    logPath->SetFont(theme::Mono(8));
    logPath->SetForegroundColour(session.levelLogDir.empty() ? theme::Faint() : theme::Text());
    logPath->SetBackgroundColour(theme::Bg());
    logPath->SetToolTip(session.levelLogDir.empty()
                            ? wxString("Recordings are saved as levels-YYYYMMDD-HHMMSS.csv, or .apl when binary is on.")
                            : wxString(session.levelLogDir));
    logPath->SetMinSize(FromDIP(wxSize(280, -1)));
    auto* browse = new FlatButton(this, "Folder", FlatButton::Kind::Quiet);
    browse->SetToolTip("Choose where level recordings are saved.");
    browse->onClick = [this, &session, apply, logPath] {
        const wxString initial = session.levelLogDir.empty() ? wxString() : wxString(session.levelLogDir);
        wxDirDialog dialog(this, "Level history folder", initial, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        session.levelLogDir = dialog.GetPath().ToStdWstring();
        logPath->SetLabel(FolderText(session.levelLogDir));
        logPath->SetForegroundColour(theme::Text());
        logPath->SetToolTip(dialog.GetPath());
        Layout();
        apply();
    };
    auto* logRow = new wxBoxSizer(wxHORIZONTAL);
    logRow->Add(logPath, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    logRow->Add(browse, 0, wxALIGN_CENTER_VERTICAL);
    auto* binary = Option(this, "Save level history as binary",
                          "Record writes a compact .apl file. Leave this off to keep writing CSV.", session.levelLogBinary,
                          [&session, apply](bool on) {
                              session.levelLogBinary = on;
                              apply();
                          });
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
    sizer->Add(logCaption, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(18));
    sizer->Add(logRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    sizer->Add(binary, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    sizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    sizer->Add(done, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(20));
    SetSizer(sizer);
    sizer->Fit(this);
    CentreOnParent();
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
}

} // namespace bv
