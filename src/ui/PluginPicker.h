#pragma once

#include "plugins/Plugin.h"
#include "plugins/Scanner.h"

#include <functional>
#include <vector>

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

namespace bv {

class PluginPicker : public wxDialog {
public:
    PluginPicker(wxWindow* parent, Scanner& scanner, std::vector<std::wstring> folders);
    bool Accepted() const { return accepted_; }
    const PluginInfo* GetSelection() const { return accepted_ ? &selection_ : nullptr; }

    std::function<void(const std::wstring&)> onAddFolder;

private:
    void Reload();
    void Accept();

    Scanner& scanner_;
    std::vector<std::wstring> folders_;
    std::vector<PluginInfo> shown_;
    PluginInfo selection_{};
    bool accepted_ = false;
    wxTextCtrl* search_ = nullptr;
    class ChoiceChips* format_ = nullptr;
    class SelectList* list_ = nullptr;
    wxStaticText* count_ = nullptr;
    wxTimer poll_;
};

} // namespace bv
