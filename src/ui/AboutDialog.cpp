#include "ui/AboutDialog.h"

#include "Theme.h"
#include "Version.h"
#include "ui/Widgets.h"

#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

namespace bv {
namespace {

wxStaticText* Line(wxWindow* parent, const wxString& text, const wxFont& font, const wxColour& colour) {
    auto* label = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    label->SetFont(font);
    label->SetForegroundColour(colour);
    label->SetBackgroundColour(theme::Bg());
    return label;
}

} // namespace

AboutDialog::AboutDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "About AudioPass", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX) {
    SetBackgroundColour(theme::Bg());
    SetForegroundColour(theme::Text());
    SetFont(theme::Ui(10));

    auto* icon = new wxStaticBitmap(this, wxID_ANY, AppIconBitmap(FromDIP(64)));
    auto* name = Line(this, "AudioPass", theme::Ui(18, true), theme::Text());
    auto* version = Line(this, wxString::Format("Version %s", kVersion), theme::Mono(10), theme::Muted());
    auto* summary = Line(this, "Microphone through VST2 and VST3\ninto a virtual cable or a WAV file.", theme::Ui(10), theme::Text());
    auto* legal = Line(this, "VST is a trademark of Steinberg Media Technologies GmbH,\nregistered in Europe and other countries.", theme::Ui(8), theme::Faint());

    auto* close = new FlatButton(this, "Close", FlatButton::Kind::Primary);
    close->onClick = [this] { EndModal(wxID_CLOSE); };

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(icon, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(22));
    sizer->Add(name, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(12));
    sizer->Add(version, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(2));
    sizer->Add(summary, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(28));
    sizer->Add(legal, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(22));
    sizer->Add(close, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(20));
    SetSizer(sizer);
    sizer->Fit(this);
    CentreOnParent();
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
}

} // namespace bv
