#pragma once

#include "Session.h"

#include <functional>

#include <wx/dialog.h>

namespace bv {

bool enableStartWithWindows(bool enabled);

class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent, Session& session, std::function<void()> apply);
};

} // namespace bv
