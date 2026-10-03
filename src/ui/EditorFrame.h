#pragma once

#include "plugins/Plugin.h"

#include <functional>

#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/timer.h>

namespace bv {

class EditorFrame : public wxFrame {
public:
    EditorFrame(wxWindow* parent, Plugin* plugin, bool resizable);
    bool Opened() const { return opened_; }
    Plugin* GetPlugin() const { return plugin_; }
    void Detach();

    std::function<void()> onClosed;

private:
    void FitPlugin(int pixelWidth, int pixelHeight);
    void OnClose(wxCloseEvent& event);

    Plugin* plugin_ = nullptr;
    wxPanel* host_ = nullptr;
    wxTimer idle_;
    int suppress_ = 0;
    bool opened_ = false;
};

class ParameterFrame : public wxFrame {
public:
    ParameterFrame(wxWindow* parent, Plugin* plugin);
    Plugin* GetPlugin() const { return plugin_; }
    void Detach();

    std::function<void()> onClosed;

private:
    void OnPaint(wxPaintEvent& event);
    Plugin* plugin_ = nullptr;
    int drag_ = -1;
    wxTimer refresh_;
};

} // namespace bv
