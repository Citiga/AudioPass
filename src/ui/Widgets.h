#pragma once

#include "Theme.h"
#include "audio/Levels.h"

#include <functional>
#include <vector>

#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/icon.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/popupwin.h>

class wxTopLevelWindow;

namespace bv {

void ApplyDarkChrome(wxWindow* window);
void ApplyAppIcon(wxTopLevelWindow* window);
void ShowLevelLog(wxWindow* parent, std::vector<LevelSample> samples, const wxString& path, int binsPerSecond);
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

class LevelHistory : public wxPanel {
public:
    enum class Trace { Both, Peak, Rms };
    enum class Scale { Db60, Db36, Db24 };

    explicit LevelHistory(wxWindow* parent);
    // UI thread. `samples` were just drained from the engine.
    void Append(const LevelSample* samples, int count);
    void Clear();
    void SetTrace(Trace trace);
    // Db60 is -60 to +6 dB, Db36 is -36 to +6 dB, Db24 is -24 to +6 dB.
    void SetScale(Scale scale);
    Trace GetTrace() const { return trace_; }
    Scale GetScale() const {
        if (floor_ < -50.f) {
            return Scale::Db60;
        }
        if (floor_ < -30.f) {
            return Scale::Db36;
        }
        return Scale::Db24;
    }
    void SetSnapshot(std::vector<LevelSample> samples, int binsPerSecond);
    void SetFrozen(bool frozen);
    bool Frozen() const { return frozen_; }

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    void InvalidateTrace();
    void NoteSample(const LevelSample& sample);
    void ScrollCommitted();
    void DrawColumn(wxDC& dc, int x, float inPeak, float inRms, float outPeak, float outRms, bool connect);
    int DbToY(float db) const;
    std::vector<LevelSample> samples_;
    wxBitmap traceBitmap_;
    Trace trace_ = Trace::Both;
    float floor_ = -60.f;
    float ceil_ = 6.f;
    bool frozen_ = false;
    bool snapshot_ = false;
    bool traceValid_ = false;
    int binsPerSecond_ = kLevelBinsPerSecond;
    int traceW_ = 0;
    int traceH_ = 0;
    size_t fed_ = 0;
    size_t dropped_ = 0;
    float carry_ = 0.f;
    float pxPerSample_ = 0.f;
    float smoothInPeak_ = -140.f;
    float smoothInRms_ = -140.f;
    float smoothOutPeak_ = -140.f;
    float smoothOutRms_ = -140.f;
    float colInPeak_ = -140.f;
    float colInRms_ = -140.f;
    float colOutPeak_ = -140.f;
    float colOutRms_ = -140.f;
    int colCount_ = 0;
    int heldInPeakY_ = -1;
    int heldInRmsY_ = -1;
    int heldOutPeakY_ = -1;
    int heldOutRmsY_ = -1;
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

class StrengthSlider : public wxPanel {
public:
    explicit StrengthSlider(wxWindow* parent);
    float GetRatio() const { return ratio_; }
    void SetRatio(float ratio);
    bool Enable(bool enable) override;

    std::function<void(float)> onChange;

private:
    wxSize DoGetBestSize() const override;
    void OnPaint(wxPaintEvent& event);
    void SetFromX(int x);
    float ratio_ = 1.f;
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
