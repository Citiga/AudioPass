#pragma once

#include <wx/dialog.h>

namespace bv {

class AboutDialog : public wxDialog {
public:
    explicit AboutDialog(wxWindow* parent);
};

} // namespace bv
