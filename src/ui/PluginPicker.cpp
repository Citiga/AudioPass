#include "ui/PluginPicker.h"

#include "Theme.h"
#include "Utf.h"
#include "ui/Widgets.h"

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace bv {

PluginPicker::PluginPicker(wxWindow* parent, Scanner& scanner, std::vector<std::wstring> folders)
    : wxDialog(parent, wxID_ANY, "Add plugin", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      scanner_(scanner),
      folders_(std::move(folders)),
      poll_(this) {
    SetBackgroundColour(theme::Bg());
    SetForegroundColour(theme::Text());
    SetFont(theme::Ui(10));
    SetClientSize(FromDIP(wxSize(460, 540)));

    search_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    search_->SetHint("Search");
    search_->SetBackgroundColour(theme::Field());
    search_->SetForegroundColour(theme::Text());
    format_ = new ChoiceChips(this, {"All", "VST3", "VST2"});
    format_->SetBackgroundColour(theme::Bg());
    list_ = new SelectList(this);
    count_ = new wxStaticText(this, wxID_ANY, "");
    count_->SetForegroundColour(theme::Muted());
    count_->SetBackgroundColour(theme::Bg());
    count_->SetFont(theme::Ui(8));

    auto* rescan = new FlatButton(this, "Rescan", FlatButton::Kind::Ghost);
    rescan->SetBackgroundColour(theme::Bg());
    auto* folder = new FlatButton(this, "Folder", FlatButton::Kind::Ghost);
    folder->SetBackgroundColour(theme::Bg());
    auto* cancel = new FlatButton(this, "Cancel", FlatButton::Kind::Ghost);
    cancel->SetBackgroundColour(theme::Bg());
    auto* add = new FlatButton(this, "Add", FlatButton::Kind::Primary);

    auto* filters = new wxBoxSizer(wxHORIZONTAL);
    filters->Add(format_, 0, wxALIGN_CENTER_VERTICAL);
    filters->AddStretchSpacer();
    filters->Add(folder, 0, wxRIGHT, FromDIP(6));
    filters->Add(rescan, 0);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(count_, 0, wxALIGN_CENTER_VERTICAL);
    buttons->AddStretchSpacer();
    buttons->Add(cancel, 0, wxRIGHT, FromDIP(8));
    buttons->Add(add, 0);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(search_, 0, wxEXPAND | wxALL, FromDIP(16));
    root->Add(filters, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    root->Add(list_, 1, wxEXPAND | wxALL, FromDIP(12));
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
    SetSizer(root);

    search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Reload(); });
    format_->onChange = [this](int) { Reload(); };
    list_->onActivate = [this](int) { Accept(); };
    rescan->onClick = [this] {
        scanner_.start(folders_, {});
        Reload();
    };
    folder->onClick = [this] {
        wxDirDialog dialog(this, "Plugin folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        folders_.push_back(dialog.GetPath().ToStdWstring());
        if (onAddFolder) {
            onAddFolder(folders_.back());
        }
        scanner_.start(folders_, {});
    };
    cancel->onClick = [this] { EndModal(wxID_CANCEL); };
    add->onClick = [this] { Accept(); };
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        poll_.Stop();
        event.Skip();
    });
    poll_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Reload(); });
    poll_.Start(400);
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
    Reload();
}

void PluginPicker::Reload() {
    const wxString query = search_->GetValue().Lower();
    const int format = format_->GetSelection();
    shown_.clear();
    std::vector<SelectRow> rows;
    for (const auto& plugin : scanner_.plugins()) {
        if (format == 1 && plugin.format != PluginFormat::Vst3) {
            continue;
        }
        if (format == 2 && plugin.format != PluginFormat::Vst2) {
            continue;
        }
        const wxString name = wxString::FromUTF8(plugin.name);
        const wxString vendor = wxString::FromUTF8(plugin.vendor);
        const wxString path = wxString::FromUTF8(plugin.path);
        if (!query.empty() && name.Lower().Find(query) == wxNOT_FOUND &&
            vendor.Lower().Find(query) == wxNOT_FOUND && path.Lower().Find(query) == wxNOT_FOUND) {
            continue;
        }
        shown_.push_back(plugin);
        SelectRow row;
        row.title = name.empty() ? path : name;
        row.subtitle = vendor.empty() ? path : vendor;
        row.tag = plugin.format == PluginFormat::Vst3 ? "VST3" : "VST2";
        rows.push_back(std::move(row));
    }
    list_->SetRows(std::move(rows));
    const wxString scanning = scanner_.scanning() ? "Indexing…  " : "";
    count_->SetLabel(scanning + wxString::Format("%zu", shown_.size()));
}

void PluginPicker::Accept() {
    const int index = list_->GetSelection();
    if (index < 0 || index >= static_cast<int>(shown_.size())) {
        return;
    }
    selection_ = shown_[static_cast<size_t>(index)];
    accepted_ = true;
    poll_.Stop();
    EndModal(wxID_OK);
}

} // namespace bv
