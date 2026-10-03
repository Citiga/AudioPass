#pragma once

#include "Theme.h"

#include <functional>
#include <vector>

#include <wx/bitmap.h>
#include <wx/icon.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/popupwin.h>

class wxTopLevelWindow;

namespace bv {

void ApplyDarkChrome(wxWindow* window);
void ApplyAppIcon(wxTopLevelWindow* window);
wxIcon AppIcon(int size);
wxBitmap AppIconBitmap(int size);

class Card : public wxPanel {
public:
    explicit Card(wxWindow* parent);
private:
    void OnPaint(wxPaintEvent& event);
};

class FlatButton : public wxPanel {
public:
    enum class Kind { Primary, Ghost, Quiet };

    FlatButton(wxWindow* parent, const wxString& label, Kind kind);
    void SetLabel(const wxString& label);
    bool Enable(bool enable) override;

    std::function<void()> onClick;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    wxString label_;
    Kind kind_;
    bool hover_ = false;
    bool down_ = false;
};

class CheckBox : public wxPanel {
public:
    CheckBox(wxWindow* parent, const wxString& label);
    bool GetValue() const { return checked_; }
    void SetValue(bool checked);

    std::function<void(bool)> onChange;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    wxString label_;
    bool checked_ = false;
    bool hover_ = false;
};

class MeterBar : public wxPanel {
public:
    explicit MeterBar(wxWindow* parent);
    void SetLevel(float peak);

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    float display_ = 0.f;
    bool clip_ = false;
    long long clipUntil_ = 0;
};

class VuMeter : public wxPanel {
public:
    explicit VuMeter(wxWindow* parent);
    void SetLevel(float peak);
    void ResetSession();

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    float needleDb_ = -40.f;
    float sessionMin_ = 0.f;
    float sessionMax_ = 0.f;
    bool haveSession_ = false;
    long long lastTick_ = 0;
};

class GainSlider : public wxPanel {
public:
    explicit GainSlider(wxWindow* parent);
    float GetDb() const { return db_; }
    void SetDb(float db);

    std::function<void(float)> onChange;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    void SetFromX(int x);
    float db_ = 0.f;
    bool dragging_ = false;
};

class ChoiceChips : public wxPanel {
public:
    ChoiceChips(wxWindow* parent, std::vector<wxString> choices);
    int GetSelection() const { return selection_; }
    void SetSelection(int index);

    std::function<void(int)> onChange;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    int Hit(const wxPoint& point) const;
    std::vector<wxString> choices_;
    int selection_ = 0;
    int hover_ = -1;
};

class DeviceField : public wxPanel {
public:
    explicit DeviceField(wxWindow* parent);
    void SetItems(std::vector<wxString> items, int selected);
    int GetSelection() const { return selection_; }

    std::function<void(int)> onSelect;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    void ShowPopup();
    std::vector<wxString> items_;
    int selection_ = -1;
    bool hover_ = false;
    bool open_ = false;
};

struct ChainRow {
    wxString title;
    wxString meta;
    bool bypassed = false;
    bool hasEditor = false;
};

class ChainList : public wxScrolled<wxPanel> {
public:
    explicit ChainList(wxWindow* parent);
    void SetRows(std::vector<ChainRow> rows);
    int GetSelection() const { return selection_; }

    std::function<void(int)> onSelect;
    std::function<void(int)> onToggleBypass;
    std::function<void(int)> onOpen;
    std::function<void(int)> onRemove;
    std::function<void(int from, int to)> onMove;

private:
    void OnPaint(wxPaintEvent& event);
    void UpdateScroll();
    void MoveBy(int index, int direction);
    wxRect ArrowRect(int row, bool up) const;
    int RowHeight() const;
    int HitRow(int y) const;
    enum class Hit { None, Row, Up, Down, Bypass, Open, Remove };
    Hit HitTest(const wxPoint& point, int& row) const;
    std::vector<ChainRow> rows_;
    int selection_ = -1;
    int hover_ = -1;
    int dragFrom_ = -1;
    int dropIndex_ = -1;
    bool dragging_ = false;
    bool down_ = false;
    wxPoint downPoint_;
};

struct SelectRow {
    wxString title;
    wxString subtitle;
    wxString tag;
};

class SelectList : public wxScrolled<wxPanel> {
public:
    explicit SelectList(wxWindow* parent);
    void SetRows(std::vector<SelectRow> rows);
    int GetSelection() const { return selection_; }
    void SetSelection(int index);

    std::function<void(int)> onSelect;
    std::function<void(int)> onActivate;

private:
    void OnPaint(wxPaintEvent& event);
    int RowHeight() const;
    int Hit(int y) const;
    std::vector<SelectRow> rows_;
    int selection_ = -1;
    int hover_ = -1;
};

} // namespace bv
