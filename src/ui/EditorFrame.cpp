#include "ui/EditorFrame.h"

#include "Theme.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>

namespace bv {

EditorFrame::EditorFrame(wxWindow* parent, Plugin* plugin, bool resizable)
    : wxFrame(parent, wxID_ANY, wxString::FromUTF8(plugin->info().name), wxDefaultPosition, wxDefaultSize,
              wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT | wxFRAME_TOOL_WINDOW |
                  (resizable ? (wxRESIZE_BORDER | wxMAXIMIZE_BOX) : 0)),
      plugin_(plugin),
      idle_(this) {
    SetBackgroundColour(theme::Bg());
    host_ = new wxPanel(this);
    host_->SetBackgroundColour(wxColour(12, 13, 16));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host_, 1, wxEXPAND);
    SetSizer(sizer);
    SetClientSize(FromDIP(wxSize(640, 420)));
    Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        if (suppress_ == 0 && plugin_ && opened_) {
            const wxSize physical = ToPhys(GetClientSize());
            plugin_->resizeEditor(physical.x, physical.y);
        }
    });
    idle_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (plugin_) {
            plugin_->idleEditor();
        }
    });
    Show();
    Layout();
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
    int width = 640;
    int height = 420;
    opened_ = plugin_->openEditor(host_->GetHWND(), width, height, [this](int w, int h) { FitPlugin(w, h); });
    if (opened_) {
        FitPlugin(width, height);
        idle_.Start(30);
    }
}

void EditorFrame::Detach() {
    plugin_ = nullptr;
    idle_.Stop();
}

void EditorFrame::FitPlugin(int pixelWidth, int pixelHeight) {
    ++suppress_;
    SetClientSize(FromPhys(wxSize(std::max(80, pixelWidth), std::max(60, pixelHeight))));
    --suppress_;
}

void EditorFrame::OnClose(wxCloseEvent& event) {
    idle_.Stop();
    if (plugin_) {
        Plugin* plugin = plugin_;
        plugin_ = nullptr;
        plugin->closeEditor();
    }
    if (onClosed) {
        onClosed();
    }
    event.Skip();
}

ParameterFrame::ParameterFrame(wxWindow* parent, Plugin* plugin)
    : wxFrame(parent, wxID_ANY, wxString::FromUTF8(plugin->info().name + "  parameters"), wxDefaultPosition,
              wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT | wxFRAME_TOOL_WINDOW),
      plugin_(plugin),
      refresh_(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Bg());
    SetClientSize(FromDIP(wxSize(360, 480)));
    Bind(wxEVT_PAINT, &ParameterFrame::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        const int row = event.GetY() / FromDIP(36);
        if (plugin_ && row >= 0 && row < plugin_->parameterCount()) {
            drag_ = row;
            CaptureMouse();
            const float value = std::clamp(event.GetX() / static_cast<float>(GetClientSize().x), 0.f, 1.f);
            plugin_->setParameterValue(row, value);
            Refresh(false);
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        if (drag_ >= 0 && plugin_) {
            const float value = std::clamp(event.GetX() / static_cast<float>(std::max(1, GetClientSize().x)), 0.f, 1.f);
            plugin_->setParameterValue(drag_, value);
            Refresh(false);
        }
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        drag_ = -1;
        if (HasCapture()) {
            ReleaseMouse();
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        refresh_.Stop();
        plugin_ = nullptr;
        if (onClosed) {
            onClosed();
        }
        event.Skip();
    });
    refresh_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Refresh(false); });
    refresh_.Start(100);
    Show();
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
}

void ParameterFrame::Detach() {
    plugin_ = nullptr;
    refresh_.Stop();
}

void ParameterFrame::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    gc->SetBrush(wxBrush(theme::Bg()));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(0, 0, size.x, size.y);
    if (!plugin_) {
        return;
    }
    const int count = plugin_->parameterCount();
    const int rowH = FromDIP(36);
    gc->SetFont(theme::Ui(9), theme::Text());
    for (int i = 0; i < count; ++i) {
        const int y = i * rowH;
        if (y > size.y) {
            break;
        }
        const float value = plugin_->parameterValue(i);
        gc->SetBrush(wxBrush(theme::Field()));
        gc->DrawRoundedRectangle(FromDIP(12), y + FromDIP(8), size.x - FromDIP(24), FromDIP(22), FromDIP(6));
        gc->SetBrush(wxBrush(theme::Accent()));
        gc->DrawRoundedRectangle(FromDIP(12), y + FromDIP(8),
                                 std::max(4, static_cast<int>((size.x - FromDIP(24)) * value)), FromDIP(22),
                                 FromDIP(6));
        gc->SetFont(theme::Ui(8, true), theme::Text());
        const wxString name = wxString::FromUTF8(plugin_->parameterName(i));
        gc->DrawText(name, FromDIP(20), y + FromDIP(11));
    }
    if (count == 0) {
        gc->SetFont(theme::Ui(10), theme::Faint());
        gc->DrawText("This plugin has no parameters.", FromDIP(16), FromDIP(16));
    }
}

} // namespace bv
