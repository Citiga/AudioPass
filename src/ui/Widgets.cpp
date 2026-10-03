#include "ui/Widgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/time.h>
#include <wx/toplevel.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwmapi.h>
#undef DrawText

namespace bv {
namespace {

void FillRound(wxGraphicsContext* gc, const wxRect& rect, double radius, const wxColour& fill,
               const wxColour& stroke = wxNullColour) {
    const double w = std::max(0, rect.width - 1);
    const double h = std::max(0, rect.height - 1);
    const double r = std::min(radius, std::min(w, h) / 2.0);
    gc->SetBrush(wxBrush(fill));
    gc->SetPen(stroke.IsOk() ? wxPen(stroke, 1) : *wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, w, h, r);
}

void RepaintOnResize(wxWindow* window) {
    window->Bind(wxEVT_SIZE, [window](wxSizeEvent& event) {
        event.Skip();
        window->Refresh(false);
    });
}

constexpr float kGainMin = -60.f;
constexpr float kGainMax = 12.f;

wxString Clip(wxDC& dc, const wxString& text, int width) {
    if (width <= 0) {
        return {};
    }
    return wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, width);
}

class DevicePopup : public wxPopupTransientWindow {
public:
    DevicePopup(wxWindow* parent, std::vector<wxString> items, int selected,
                std::function<void(int)> onPick)
        : wxPopupTransientWindow(parent, wxBORDER_NONE),
          items_(std::move(items)),
          selected_(selected),
          onPick_(std::move(onPick)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        const int rows = std::clamp(static_cast<int>(items_.size()), 1, 8);
        SetClientSize(parent->GetSize().x, rows * RowHeight() + FromDIP(8));
        Bind(wxEVT_PAINT, &DevicePopup::OnPaint, this);
        RepaintOnResize(this);
        Bind(wxEVT_MOTION, &DevicePopup::OnMotion, this);
        Bind(wxEVT_LEFT_UP, &DevicePopup::OnClick, this);
        Bind(wxEVT_MOUSEWHEEL, &DevicePopup::OnWheel, this);
    }

    std::function<void()> onClose;

    void Dismiss() override {
        wxPopupTransientWindow::Dismiss();
        if (onClose) {
            auto close = std::move(onClose);
            close();
        }
        Destroy();
    }

private:
    int RowHeight() const { return FromDIP(32); }
    int Visible() const { return std::max(1, (GetClientSize().y - FromDIP(8)) / RowHeight()); }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        const wxSize size = GetClientSize();
        dc.SetBrush(theme::Field());
        dc.SetPen(wxPen(theme::Line()));
        dc.DrawRectangle(0, 0, size.x, size.y);
        dc.SetFont(theme::Ui(10));
        const int rowH = RowHeight();
        const int first = scroll_;
        const int last = std::min(static_cast<int>(items_.size()), first + Visible());
        for (int i = first; i < last; ++i) {
            const int y = FromDIP(4) + (i - first) * rowH;
            if (i == hover_ || i == selected_) {
                dc.SetBrush(i == selected_ ? wxColour(28, 48, 42) : theme::Hover());
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(FromDIP(4), y, size.x - FromDIP(8), rowH - FromDIP(2));
            }
            dc.SetTextForeground(i == selected_ ? theme::Accent() : theme::Text());
            const wxString label = Clip(dc, items_[static_cast<size_t>(i)], size.x - FromDIP(24));
            dc.DrawText(label, FromDIP(12), y + (rowH - dc.GetCharHeight()) / 2 - FromDIP(1));
        }
    }

    void OnMotion(wxMouseEvent& event) {
        const int row = RowAt(event.GetY());
        if (row != hover_) {
            hover_ = row;
            Refresh(false);
        }
    }

    void OnClick(wxMouseEvent& event) {
        const int row = RowAt(event.GetY());
        if (row >= 0 && onPick_) {
            onPick_(row);
        }
        Dismiss();
    }

    void OnWheel(wxMouseEvent& event) {
        const int maxScroll = std::max(0, static_cast<int>(items_.size()) - Visible());
        scroll_ = std::clamp(scroll_ + (event.GetWheelRotation() > 0 ? -1 : 1), 0, maxScroll);
        Refresh(false);
    }

    int RowAt(int y) const {
        const int index = scroll_ + (y - FromDIP(4)) / RowHeight();
        if (index < 0 || index >= static_cast<int>(items_.size())) {
            return -1;
        }
        return index;
    }

    std::vector<wxString> items_;
    int selected_ = -1;
    int hover_ = -1;
    int scroll_ = 0;
    std::function<void(int)> onPick_;
};

} // namespace

void ApplyDarkChrome(wxWindow* window) {
    if (!window) {
        return;
    }
    const HWND hwnd = window->GetHWND();
    if (!hwnd) {
        return;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const COLORREF caption = RGB(14, 15, 18);
    const COLORREF text = RGB(243, 244, 246);
    const COLORREF border = RGB(42, 46, 54);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

void ApplyAppIcon(wxTopLevelWindow* window) {
    if (!window) {
        return;
    }
    const HINSTANCE module = GetModuleHandleW(nullptr);
    wxIconBundle bundle;
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    for (int size : sizes) {
        const auto handle = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        if (!handle) {
            continue;
        }
        wxIcon icon;
        if (!icon.CreateFromHICON(handle)) {
            DestroyIcon(handle);
            continue;
        }
        bundle.AddIcon(icon);
    }
    if (bundle.IsOk()) {
        window->SetIcons(bundle);
    }
}

wxIcon AppIcon(int size) {
    const auto handle = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
    wxIcon icon;
    if (handle && !icon.CreateFromHICON(handle)) {
        DestroyIcon(handle);
    }
    return icon;
}

wxBitmap AppIconBitmap(int size) {
    const wxIcon icon = AppIcon(size);
    if (!icon.IsOk()) {
        return wxNullBitmap;
    }
    wxBitmap bitmap;
    bitmap.CopyFromIcon(icon);
    return bitmap;
}

Card::Card(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Elevated());
    Bind(wxEVT_PAINT, &Card::OnPaint, this);
    RepaintOnResize(this);
}

void Card::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    FillRound(gc.get(), wxRect(0, 0, size.x, size.y), FromDIP(12), theme::Elevated(), theme::Line());
}

FlatButton::FlatButton(wxWindow* parent, const wxString& label, Kind kind)
    : wxPanel(parent), label_(label), kind_(kind) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour());
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetFont(theme::Ui(10, true));
    Bind(wxEVT_PAINT, &FlatButton::OnPaint, this);
    RepaintOnResize(this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        hover_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hover_ = false;
        down_ = false;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        down_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const bool fire = down_ && IsEnabled() && GetClientRect().Contains(event.GetPosition());
        down_ = false;
        Refresh(false);
        if (fire && onClick) {
            onClick();
        }
    });
}

void FlatButton::SetLabel(const wxString& label) {
    label_ = label;
    InvalidateBestSize();
    Refresh(false);
}

bool FlatButton::Enable(bool enable) {
    const bool changed = wxPanel::Enable(enable);
    Refresh(false);
    return changed;
}

wxSize FlatButton::DoGetBestSize() const {
    const wxSize text = GetTextExtent(label_);
    return wxSize(text.x + FromDIP(28), FromDIP(30));
}

void FlatButton::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    wxColour fill = theme::Elevated();
    wxColour text = theme::Text();
    wxColour stroke = theme::Line();
    if (!IsEnabled()) {
        text = theme::Faint();
        stroke = theme::Line();
    } else if (kind_ == Kind::Primary) {
        fill = down_ ? wxColour(47, 186, 148) : theme::Accent();
        text = theme::AccentInk();
        stroke = fill;
    } else if (hover_ || down_) {
        fill = theme::Hover();
        text = kind_ == Kind::Quiet ? theme::Muted() : theme::Text();
    } else if (kind_ == Kind::Quiet) {
        fill = theme::Elevated();
        text = theme::Accent();
        stroke = wxColour(0, 0, 0, 0);
    }
    FillRound(gc.get(), wxRect(0, 0, size.x, size.y), FromDIP(8), fill, stroke);
    gc->SetFont(GetFont(), text);
    double tw = 0, th = 0;
    gc->GetTextExtent(label_, &tw, &th);
    gc->DrawText(label_, (size.x - tw) / 2.0, (size.y - th) / 2.0 - 1);
}

CheckBox::CheckBox(wxWindow* parent, const wxString& label) : wxPanel(parent), label_(label) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour());
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetFont(theme::Ui(10));
    Bind(wxEVT_PAINT, &CheckBox::OnPaint, this);
    RepaintOnResize(this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        hover_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hover_ = false;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        if (!GetClientRect().Contains(event.GetPosition())) {
            return;
        }
        SetValue(!checked_);
        if (onChange) {
            onChange(checked_);
        }
    });
}

void CheckBox::SetValue(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    Refresh(false);
}

wxSize CheckBox::DoGetBestSize() const {
    const wxSize text = GetTextExtent(label_);
    return wxSize(FromDIP(16) + FromDIP(8) + text.x + FromDIP(4), std::max(FromDIP(30), text.y));
}

void CheckBox::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    const int box = FromDIP(16);
    const int x = 0;
    const int y = (size.y - box) / 2;
    const wxColour fill = checked_ ? theme::Accent() : hover_ ? theme::Hover() : theme::Field();
    const wxColour stroke = checked_ ? theme::Accent() : theme::Line();
    FillRound(gc.get(), wxRect(x, y, box, box), FromDIP(4), fill, stroke);
    if (checked_) {
        gc->SetPen(wxPen(theme::AccentInk(), std::max(1.5, FromDIP(2) * 0.8)));
        const double left = x + box * 0.22;
        const double mid = y + box * 0.55;
        const double right = x + box * 0.78;
        const double top = y + box * 0.28;
        gc->StrokeLine(left, y + box * 0.50, x + box * 0.42, mid);
        gc->StrokeLine(x + box * 0.42, mid, right, top);
    }
    gc->SetFont(GetFont(), hover_ || checked_ ? theme::Text() : theme::Muted());
    double tw = 0;
    double th = 0;
    gc->GetTextExtent(label_, &tw, &th);
    gc->DrawText(label_, box + FromDIP(8), (size.y - th) / 2.0 - 1);
}

MeterBar::MeterBar(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Elevated());
    Bind(wxEVT_PAINT, &MeterBar::OnPaint, this);
    RepaintOnResize(this);
}

void MeterBar::SetLevel(float peak) {
    peak = std::clamp(peak, 0.f, 4.f);
    float shaped = 0.f;
    if (peak > 0.00001f) {
        const float db = 20.f * std::log10(peak);
        shaped = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
    }
    display_ = std::max(shaped, display_ * 0.84f);
    const long long now = wxGetUTCTimeMillis().GetValue();
    if (peak >= 0.98f) {
        clipUntil_ = now + 900;
        clip_ = true;
    } else if (clip_ && now > clipUntil_) {
        clip_ = false;
    }
    Refresh(false);
}

wxSize MeterBar::DoGetBestSize() const {
    return wxSize(FromDIP(40), FromDIP(12));
}

void MeterBar::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    const double radius = size.y / 2.0;
    FillRound(gc.get(), wxRect(0, 0, size.x, size.y), radius, theme::Field());
    const double span = std::max(0.0, static_cast<double>(size.x) - 4.0);
    const double width = span * std::clamp(static_cast<double>(display_), 0.0, 1.0);
    if (width > 0.5) {
        const wxColour colour = clip_ ? theme::Danger() : theme::Accent();
        const double barH = std::max(1.0, static_cast<double>(size.y) - 4.0);
        gc->SetBrush(wxBrush(colour));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(2.0, 2.0, width, barH, std::min(radius, barH / 2.0));
    }
}

namespace {

constexpr float kVuFloor = -40.f;
constexpr float kVuCeil = 0.f;

float VuNorm(float db) {
    return std::clamp((db - kVuFloor) / (kVuCeil - kVuFloor), 0.f, 1.f);
}

wxColour VuColour(float db, bool lit) {
    const wxColour on = db >= -3.f ? theme::Danger() : db >= -12.f ? theme::Warn() : theme::Accent();
    if (lit) {
        return on;
    }
    return wxColour(on.Red() / 5, on.Green() / 5, on.Blue() / 5);
}

} // namespace

VuMeter::VuMeter(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Elevated());
    Bind(wxEVT_PAINT, &VuMeter::OnPaint, this);
    RepaintOnResize(this);
}

void VuMeter::ResetSession() {
    haveSession_ = false;
    sessionMin_ = 0.f;
    sessionMax_ = 0.f;
    Refresh(false);
}

void VuMeter::SetLevel(float peak) {
    peak = std::clamp(peak, 0.f, 4.f);
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    float dt = 0.033f;
    if (lastTick_ != 0) {
        dt = std::clamp(static_cast<float>(now - lastTick_) / 1000.f, 0.001f, 0.1f);
    }
    lastTick_ = now;
    const float target = peak > 1.e-5f ? 20.f * std::log10(peak) : kVuFloor;
    const float coeff = 1.f - std::exp(-dt / 0.3f);
    needleDb_ += (target - needleDb_) * coeff;

    if (peak > 1.e-5f) {
        const float db = 20.f * std::log10(peak);
        if (!haveSession_) {
            sessionMin_ = sessionMax_ = db;
            haveSession_ = true;
        } else {
            sessionMin_ = std::min(sessionMin_, db);
            sessionMax_ = std::max(sessionMax_, db);
        }
    }
    Refresh(false);
}

wxSize VuMeter::DoGetBestSize() const {
    return wxSize(FromDIP(180), FromDIP(54));
}

void VuMeter::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    const wxSize size = GetClientSize();
    constexpr int kSegments = 28;
    const int gap = std::max(1, FromDIP(2));
    const int labelH = FromDIP(12);
    const int trackH = FromDIP(10);
    const int trackY = labelH + FromDIP(3);
    const int span = std::max(kSegments, size.x);
    const int segW = std::max(2, (span - gap * (kSegments - 1)) / kSegments);
    const int used = kSegments * segW + (kSegments - 1) * gap;
    const int origin = std::max(0, (size.x - used) / 2);

    dc.SetFont(theme::Ui(7));
    dc.SetTextForeground(theme::Faint());
    const float marks[] = {-40.f, -20.f, -12.f, -6.f, 0.f};
    const wxString names[] = {"-40", "-20", "-12", "-6", "0"};
    for (int i = 0; i < 5; ++i) {
        int x = origin + static_cast<int>(std::lround(VuNorm(marks[i]) * std::max(0, used - segW)));
        const wxSize extent = dc.GetTextExtent(names[i]);
        if (i == 0) {
            x = origin;
        } else if (i == 4) {
            x = origin + used - extent.x;
        } else {
            x -= extent.x / 2;
        }
        x = std::clamp(x, 0, std::max(0, size.x - extent.x));
        dc.DrawText(names[i], x, 0);
    }

    dc.SetPen(*wxTRANSPARENT_PEN);
    for (int i = 0; i < kSegments; ++i) {
        const float left = kVuFloor + (kVuCeil - kVuFloor) * (static_cast<float>(i) / kSegments);
        const float db = kVuFloor + (kVuCeil - kVuFloor) * (static_cast<float>(i + 1) / kSegments);
        const bool lit = needleDb_ > left + 0.05f;
        dc.SetBrush(VuColour(db, lit));
        const int x = origin + i * (segW + gap);
        dc.DrawRectangle(x, trackY, segW, trackH);
    }

    const int digitY = trackY + trackH + FromDIP(5);
    dc.SetFont(theme::Mono(8));
    const wxString minText = haveSession_ ? wxString::Format("min  %+.1f dB", sessionMin_) : wxString("min  --.- dB");
    const wxString maxText = haveSession_ ? wxString::Format("max  %+.1f dB", sessionMax_) : wxString("max  --.- dB");
    dc.SetTextForeground(theme::Muted());
    dc.DrawText(minText, origin, digitY);
    const wxSize maxExtent = dc.GetTextExtent(maxText);
    const bool hot = haveSession_ && sessionMax_ >= -0.1f;
    dc.SetTextForeground(hot ? theme::Danger() : theme::Text());
    dc.DrawText(maxText, origin + used - maxExtent.x, digitY);
}

GainSlider::GainSlider(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour());
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetToolTip("Output level, -60 to +12 dB. Double-click to reset.");
    Bind(wxEVT_PAINT, &GainSlider::OnPaint, this);
    RepaintOnResize(this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        dragging_ = true;
        CaptureMouse();
        SetFromX(event.GetX());
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&) {
        SetDb(0.f);
        if (onChange) {
            onChange(db_);
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        if (dragging_) {
            SetFromX(event.GetX());
        }
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        if (dragging_ && HasCapture()) {
            ReleaseMouse();
        }
        dragging_ = false;
    });
}

void GainSlider::SetDb(float db) {
    db_ = std::clamp(db, kGainMin, kGainMax);
    Refresh(false);
}

wxSize GainSlider::DoGetBestSize() const {
    return wxSize(FromDIP(180), FromDIP(28));
}

void GainSlider::SetFromX(int x) {
    const int pad = FromDIP(8);
    const int width = std::max(1, GetClientSize().x - pad * 2);
    const float t = std::clamp((x - pad) / static_cast<float>(width), 0.f, 1.f);
    SetDb(kGainMin + t * (kGainMax - kGainMin));
    if (onChange) {
        onChange(db_);
    }
}

void GainSlider::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    const int pad = FromDIP(8);
    const int cy = size.y / 2;
    const double span = std::max(1, size.x - pad * 2);
    const double zero = pad + (0.f - kGainMin) / (kGainMax - kGainMin) * span;
    gc->SetPen(wxPen(theme::Line(), FromDIP(4)));
    gc->StrokeLine(pad, cy, size.x - pad, cy);
    const float t = (db_ - kGainMin) / (kGainMax - kGainMin);
    const double x = pad + t * span;
    gc->SetPen(wxPen(theme::Accent(), FromDIP(4)));
    gc->StrokeLine(std::min(x, zero), cy, std::max(x, zero), cy);
    gc->SetPen(wxPen(theme::Faint(), 1));
    gc->StrokeLine(zero, cy - FromDIP(7), zero, cy + FromDIP(7));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(db_ > 0.05f ? theme::Warn() : theme::Accent()));
    const double knob = FromDIP(12);
    gc->DrawEllipse(x - knob / 2.0, cy - knob / 2.0, knob, knob);
}

ChoiceChips::ChoiceChips(wxWindow* parent, std::vector<wxString> choices)
    : wxPanel(parent), choices_(std::move(choices)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(parent->GetBackgroundColour());
    SetFont(theme::Ui(9, true));
    Bind(wxEVT_PAINT, &ChoiceChips::OnPaint, this);
    RepaintOnResize(this);
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const int hit = Hit(event.GetPosition());
        if (hit != hover_) {
            hover_ = hit;
            Refresh(false);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hover_ = -1;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const int hit = Hit(event.GetPosition());
        if (hit >= 0 && hit != selection_) {
            selection_ = hit;
            Refresh(false);
            if (onChange) {
                onChange(selection_);
            }
        }
    });
}

void ChoiceChips::SetSelection(int index) {
    selection_ = std::clamp(index, 0, static_cast<int>(choices_.size()) - 1);
    Refresh(false);
}

wxSize ChoiceChips::DoGetBestSize() const {
    int slot = FromDIP(46);
    for (const auto& choice : choices_) {
        slot = std::max(slot, GetTextExtent(choice).x + FromDIP(18));
    }
    return wxSize(slot * static_cast<int>(std::max<size_t>(choices_.size(), 1)), FromDIP(28));
}

int ChoiceChips::Hit(const wxPoint& point) const {
    if (choices_.empty() || point.y < 0 || point.y > GetClientSize().y) {
        return -1;
    }
    const int width = GetClientSize().x / static_cast<int>(choices_.size());
    const int index = point.x / std::max(1, width);
    if (index < 0 || index >= static_cast<int>(choices_.size())) {
        return -1;
    }
    return index;
}

void ChoiceChips::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc || choices_.empty()) {
        return;
    }
    const wxSize size = GetClientSize();
    FillRound(gc.get(), wxRect(0, 0, size.x, size.y), FromDIP(8), theme::Field(), theme::Line());
    const double width = static_cast<double>(size.x) / static_cast<double>(choices_.size());
    gc->SetFont(GetFont(), theme::Text());
    for (int i = 0; i < static_cast<int>(choices_.size()); ++i) {
        const double x = i * width;
        if (i == selection_) {
            gc->SetBrush(wxBrush(wxColour(28, 56, 48)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x + 2, 2, width - 4, size.y - 4, FromDIP(6));
        } else if (i == hover_) {
            gc->SetBrush(wxBrush(theme::Hover()));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x + 2, 2, width - 4, size.y - 4, FromDIP(6));
        }
        double tw = 0, th = 0;
        gc->SetFont(GetFont(), i == selection_ ? theme::Accent() : theme::Muted());
        gc->GetTextExtent(choices_[static_cast<size_t>(i)], &tw, &th);
        gc->DrawText(choices_[static_cast<size_t>(i)], x + (width - tw) / 2.0, (size.y - th) / 2.0 - 1);
    }
}

DeviceField::DeviceField(wxWindow* parent) : wxPanel(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Elevated());
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetFont(theme::Ui(10));
    Bind(wxEVT_PAINT, &DeviceField::OnPaint, this);
    RepaintOnResize(this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        hover_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hover_ = false;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { ShowPopup(); });
}

void DeviceField::SetItems(std::vector<wxString> items, int selected) {
    items_ = std::move(items);
    selection_ = items_.empty() ? -1 : std::clamp(selected, -1, static_cast<int>(items_.size()) - 1);
    Refresh(false);
}

wxSize DeviceField::DoGetBestSize() const {
    return wxSize(FromDIP(180), FromDIP(36));
}

void DeviceField::ShowPopup() {
    if (items_.empty() || open_) {
        return;
    }
    open_ = true;
    Refresh(false);
    auto* popup = new DevicePopup(this, items_, selection_, [this](int index) {
        selection_ = index;
        if (onSelect) {
            onSelect(index);
        }
        Refresh(false);
    });
    popup->onClose = [this] {
        open_ = false;
        Refresh(false);
    };
    const wxPoint origin = ClientToScreen(wxPoint(0, 0));
    popup->Position(origin, GetSize());
    popup->Popup();
}

void DeviceField::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const wxSize size = GetClientSize();
    const wxColour stroke = open_ || hover_ ? theme::Accent() : theme::Line();
    FillRound(gc.get(), wxRect(0, 0, size.x, size.y), FromDIP(8), theme::Field(), stroke);
    const wxString label = selection_ >= 0 && selection_ < static_cast<int>(items_.size())
                               ? items_[static_cast<size_t>(selection_)]
                               : wxString("Select a device");
    gc->SetFont(GetFont(), selection_ >= 0 ? theme::Text() : theme::Faint());
    double tw = 0, th = 0;
    dc.SetFont(GetFont());
    const wxString shown = Clip(dc, label, size.x - FromDIP(36));
    gc->GetTextExtent(shown, &tw, &th);
    gc->DrawText(shown, FromDIP(12), (size.y - th) / 2.0 - 1);
    gc->SetPen(wxPen(theme::Muted(), 1.4));
    const double x = size.x - FromDIP(18);
    const double y = size.y / 2.0;
    gc->StrokeLine(x - 4, y - 2, x, y + 2);
    gc->StrokeLine(x + 4, y - 2, x, y + 2);
}

ChainList::ChainList(wxWindow* parent) : wxScrolled<wxPanel>(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Elevated());
    SetScrollRate(0, FromDIP(14));
    SetCanFocus(true);
    Bind(wxEVT_PAINT, &ChainList::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        UpdateScroll();
        Refresh(false);
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        SetFocus();
        int row = -1;
        const auto hit = HitTest(CalcUnscrolledPosition(event.GetPosition()), row);
        down_ = true;
        downPoint_ = event.GetPosition();
        dragFrom_ = row;
        dragging_ = false;
        if (hit == Hit::Row && row >= 0) {
            selection_ = row;
            if (onSelect) {
                onSelect(row);
            }
            CaptureMouse();
            Refresh(false);
        } else if (row >= 0) {
            selection_ = row;
            Refresh(false);
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const wxPoint logical = CalcUnscrolledPosition(event.GetPosition());
        int row = -1;
        HitTest(logical, row);
        if (down_ && dragFrom_ >= 0 && !dragging_) {
            if (std::abs(event.GetY() - downPoint_.y) > FromDIP(4)) {
                dragging_ = true;
            }
        }
        if (dragging_) {
            const int height = RowHeight();
            dropIndex_ = std::clamp((logical.y + height / 2) / height, 0, static_cast<int>(rows_.size()));
        }
        if (row != hover_ || dragging_) {
            hover_ = row;
            Refresh(false);
        }
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        int row = -1;
        const auto hit = HitTest(CalcUnscrolledPosition(event.GetPosition()), row);
        if (HasCapture()) {
            ReleaseMouse();
        }
        if (dragging_ && dragFrom_ >= 0 && onMove) {
            onMove(dragFrom_, dropIndex_);
        } else if (!dragging_ && row >= 0) {
            if (hit == Hit::Up) {
                MoveBy(row, -1);
            } else if (hit == Hit::Down) {
                MoveBy(row, 1);
            } else if (hit == Hit::Bypass && onToggleBypass) {
                onToggleBypass(row);
            } else if (hit == Hit::Open && onOpen) {
                onOpen(row);
            } else if (hit == Hit::Remove && onRemove) {
                onRemove(row);
            }
        }
        down_ = false;
        dragging_ = false;
        dragFrom_ = -1;
        dropIndex_ = -1;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event) {
        int row = -1;
        if (HitTest(CalcUnscrolledPosition(event.GetPosition()), row) == Hit::Row && row >= 0 && onOpen) {
            onOpen(row);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        if (!dragging_) {
            hover_ = -1;
            Refresh(false);
        }
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_UP && selection_ >= 0) {
            MoveBy(selection_, -1);
            return;
        }
        if (event.GetKeyCode() == WXK_DOWN && selection_ >= 0) {
            MoveBy(selection_, 1);
            return;
        }
        if ((event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_BACK) && selection_ >= 0 &&
            onRemove) {
            onRemove(selection_);
            return;
        }
        event.Skip();
    });
    SetToolTip("Drag a plugin to reorder it, or use the arrows and the Up and Down keys.");
}

void ChainList::SetRows(std::vector<ChainRow> rows) {
    rows_ = std::move(rows);
    if (selection_ >= static_cast<int>(rows_.size())) {
        selection_ = static_cast<int>(rows_.size()) - 1;
    }
    UpdateScroll();
    Refresh(false);
}

void ChainList::UpdateScroll() {
    const int height = std::max(GetClientSize().y, static_cast<int>(rows_.size()) * RowHeight() + FromDIP(8));
    SetVirtualSize(-1, height);
}

void ChainList::MoveBy(int index, int direction) {
    if (!onMove || index < 0 || index >= static_cast<int>(rows_.size()) || direction == 0) {
        return;
    }
    if (direction < 0) {
        if (index == 0) {
            return;
        }
        selection_ = index - 1;
        onMove(index, index - 1);
    } else {
        if (index + 1 >= static_cast<int>(rows_.size())) {
            return;
        }
        selection_ = index + 1;
        onMove(index, index + 2);
    }
    int unitX = 1;
    int unitY = 1;
    GetScrollPixelsPerUnit(&unitX, &unitY);
    unitY = std::max(1, unitY);
    const int top = GetViewStart().y * unitY;
    const int y = selection_ * RowHeight();
    if (y < top) {
        Scroll(-1, y / unitY);
    } else if (y + RowHeight() > top + GetClientSize().y) {
        Scroll(-1, (y + RowHeight() - GetClientSize().y) / unitY);
    }
}

wxRect ChainList::ArrowRect(int row, bool up) const {
    const int size = FromDIP(18);
    const int gap = FromDIP(4);
    const int mid = row * RowHeight() + RowHeight() / 2;
    const int x = FromDIP(16);
    const int y = up ? mid - gap / 2 - size : mid + gap / 2;
    return wxRect(x, y, size, size);
}

int ChainList::RowHeight() const {
    return FromDIP(54);
}

int ChainList::HitRow(int y) const {
    if (rows_.empty() || y < 0) {
        return -1;
    }
    const int row = y / RowHeight();
    return row >= 0 && row < static_cast<int>(rows_.size()) ? row : -1;
}

ChainList::Hit ChainList::HitTest(const wxPoint& point, int& row) const {
    row = HitRow(point.y);
    if (row < 0) {
        return Hit::None;
    }
    const int right = GetClientSize().x - FromDIP(10);
    const int y = row * RowHeight();
    const int mid = y + RowHeight() / 2;
    auto contains = [&](int cx, int cy, int size) {
        return std::abs(point.x - cx) <= size / 2 && std::abs(point.y - cy) <= size / 2;
    };
    if (ArrowRect(row, true).Contains(point)) {
        return Hit::Up;
    }
    if (ArrowRect(row, false).Contains(point)) {
        return Hit::Down;
    }
    if (contains(right - FromDIP(12), mid, FromDIP(24))) {
        return Hit::Remove;
    }
    if (rows_[static_cast<size_t>(row)].hasEditor && contains(right - FromDIP(40), mid, FromDIP(24))) {
        return Hit::Open;
    }
    if (contains(right - FromDIP(78), mid, FromDIP(34))) {
        return Hit::Bypass;
    }
    return Hit::Row;
}

void ChainList::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    DoPrepareDC(dc);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const int width = GetClientSize().x;
    const int height = std::max(GetVirtualSize().y, GetClientSize().y);
    gc->SetBrush(wxBrush(theme::Elevated()));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(0, 0, width, height);
    if (rows_.empty()) {
        gc->SetFont(theme::Ui(10), theme::Faint());
        const wxString text = "Empty chain. The microphone is passed through.";
        double tw = 0, th = 0;
        gc->GetTextExtent(text, &tw, &th);
        gc->DrawText(text, std::max(12.0, (width - tw) / 2.0), FromDIP(28));
        return;
    }
    const int rowH = RowHeight();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const auto& row = rows_[static_cast<size_t>(i)];
        const int y = i * rowH;
        if (i == selection_ || i == hover_) {
            gc->SetBrush(wxBrush(i == selection_ ? wxColour(30, 34, 42) : theme::Hover()));
            gc->DrawRoundedRectangle(FromDIP(8), y + FromDIP(4), width - FromDIP(16), rowH - FromDIP(8),
                                     FromDIP(8));
        }
        if (i == selection_) {
            gc->SetBrush(wxBrush(theme::Accent()));
            gc->DrawRoundedRectangle(FromDIP(8), y + FromDIP(10), FromDIP(3), rowH - FromDIP(20), FromDIP(2));
        }
        const int mid = y + rowH / 2;
        auto arrowIcon = [&](const wxRect& rect, bool up, bool enabled) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(enabled ? wxColour(32, 56, 48) : theme::Field()));
            gc->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, FromDIP(5));
            const double inset = rect.width * 0.28;
            wxGraphicsPath path = gc->CreatePath();
            if (up) {
                path.MoveToPoint(rect.x + rect.width / 2.0, rect.y + inset);
                path.AddLineToPoint(rect.x + rect.width - inset, rect.y + rect.height - inset);
                path.AddLineToPoint(rect.x + inset, rect.y + rect.height - inset);
            } else {
                path.MoveToPoint(rect.x + inset, rect.y + inset);
                path.AddLineToPoint(rect.x + rect.width - inset, rect.y + inset);
                path.AddLineToPoint(rect.x + rect.width / 2.0, rect.y + rect.height - inset);
            }
            path.CloseSubpath();
            gc->SetBrush(wxBrush(enabled ? theme::Accent() : theme::Line()));
            gc->FillPath(path);
        };
        arrowIcon(ArrowRect(i, true), true, i > 0);
        arrowIcon(ArrowRect(i, false), false, i + 1 < static_cast<int>(rows_.size()));

        gc->SetFont(theme::Ui(10, true), row.bypassed ? theme::Faint() : theme::Text());
        double tw = 0, th = 0;
        gc->GetTextExtent(row.title, &tw, &th);
        gc->DrawText(row.title, FromDIP(42), y + FromDIP(8));
        gc->SetFont(theme::Ui(8), theme::Faint());
        const wxString meta = row.meta.empty() ? wxString(row.bypassed ? "Bypassed" : "Active") : row.meta;
        gc->DrawText(meta, FromDIP(42), y + FromDIP(28));
        (void)tw;

        const int switchX = width - FromDIP(88);
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(row.bypassed ? theme::Field() : theme::Accent()));
        gc->DrawRoundedRectangle(switchX, mid - FromDIP(8), FromDIP(30), FromDIP(16), FromDIP(8));
        gc->SetBrush(wxBrush(row.bypassed ? theme::Faint() : theme::AccentInk()));
        const int knob = row.bypassed ? switchX + FromDIP(2) : switchX + FromDIP(16);
        gc->DrawEllipse(knob, mid - FromDIP(6), FromDIP(12), FromDIP(12));

        if (row.hasEditor) {
            gc->SetFont(theme::Ui(8, true), theme::Muted());
            gc->DrawText("UI", width - FromDIP(50), mid - FromDIP(7));
        }
        gc->SetPen(wxPen(theme::Faint(), 1.4));
        const double x = width - FromDIP(22);
        const double cy = mid;
        gc->StrokeLine(x - 4, cy - 4, x + 4, cy + 4);
        gc->StrokeLine(x + 4, cy - 4, x - 4, cy + 4);
    }
    if (dragging_ && dropIndex_ >= 0) {
        gc->SetPen(wxPen(theme::Accent(), 2));
        const int y = dropIndex_ * rowH + FromDIP(2);
        gc->StrokeLine(FromDIP(16), y, width - FromDIP(16), y);
    }
}

SelectList::SelectList(wxWindow* parent) : wxScrolled<wxPanel>(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(theme::Bg());
    SetScrollRate(0, FromDIP(16));
    Bind(wxEVT_PAINT, &SelectList::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        const int height = std::max(GetClientSize().y, static_cast<int>(rows_.size()) * RowHeight() + FromDIP(8));
        SetVirtualSize(-1, height);
        Refresh(false);
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        const int row = Hit(CalcUnscrolledPosition(event.GetPosition()).y);
        if (row != hover_) {
            hover_ = row;
            Refresh(false);
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hover_ = -1;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        const int row = Hit(CalcUnscrolledPosition(event.GetPosition()).y);
        if (row >= 0) {
            selection_ = row;
            Refresh(false);
            if (onSelect) {
                onSelect(row);
            }
        }
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& event) {
        const int row = Hit(CalcUnscrolledPosition(event.GetPosition()).y);
        if (row >= 0) {
            selection_ = row;
            if (onActivate) {
                onActivate(row);
            }
        }
    });
}

void SelectList::SetRows(std::vector<SelectRow> rows) {
    rows_ = std::move(rows);
    if (selection_ >= static_cast<int>(rows_.size())) {
        selection_ = static_cast<int>(rows_.size()) - 1;
    }
    const int height = std::max(GetClientSize().y, static_cast<int>(rows_.size()) * RowHeight() + FromDIP(8));
    SetVirtualSize(-1, height);
    Refresh(false);
}

void SelectList::SetSelection(int index) {
    selection_ = index;
    Refresh(false);
}

int SelectList::RowHeight() const {
    return FromDIP(48);
}

int SelectList::Hit(int y) const {
    if (y < 0 || rows_.empty()) {
        return -1;
    }
    const int row = y / RowHeight();
    return row >= 0 && row < static_cast<int>(rows_.size()) ? row : -1;
}

void SelectList::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    DoPrepareDC(dc);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        return;
    }
    const int width = GetClientSize().x;
    const int height = std::max(GetVirtualSize().y, GetClientSize().y);
    gc->SetBrush(wxBrush(theme::Bg()));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(0, 0, width, height);
    if (rows_.empty()) {
        gc->SetFont(theme::Ui(10), theme::Faint());
        gc->DrawText("No plugins found.", FromDIP(16), FromDIP(16));
        return;
    }
    const int rowH = RowHeight();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const auto& row = rows_[static_cast<size_t>(i)];
        const int y = i * rowH;
        if (i == selection_ || i == hover_) {
            gc->SetBrush(wxBrush(i == selection_ ? wxColour(28, 48, 42) : theme::Hover()));
            gc->DrawRoundedRectangle(FromDIP(8), y + FromDIP(3), width - FromDIP(16), rowH - FromDIP(6),
                                     FromDIP(8));
        }
        gc->SetFont(theme::Ui(10, true), theme::Text());
        gc->DrawText(row.title, FromDIP(16), y + FromDIP(6));
        gc->SetFont(theme::Ui(8), theme::Faint());
        gc->DrawText(row.subtitle, FromDIP(16), y + FromDIP(26));
        if (!row.tag.empty()) {
            gc->SetFont(theme::Ui(8, true), theme::Accent());
            double tw = 0, th = 0;
            gc->GetTextExtent(row.tag, &tw, &th);
            gc->DrawText(row.tag, width - tw - FromDIP(16), y + FromDIP(14));
        }
    }
}

} // namespace bv
