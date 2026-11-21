#pragma once

#include "Session.h"
#include "audio/Engine.h"
#include "plugins/Scanner.h"
#include "ui/EditorFrame.h"
#include "ui/LevelFile.h"
#include "ui/Widgets.h"

#include <memory>
#include <utility>
#include <vector>

#include <wx/frame.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace bv {

class TrayIcon;

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    struct Slot {
        std::unique_ptr<Plugin> plugin;
        std::string state;
    };

    void Build();
    void LoadDevices();
    void RestoreChain();
    void RefreshChain();
    void SyncChain();
    template <typename Edit>
    void EditChain(Edit&& edit) {
        const bool live = engine_.running();
        if (live) {
            engine_.suspend();
        }
        std::forward<Edit>(edit)();
        SyncChain();
        if (live) {
            engine_.resume();
        }
    }
    void ToggleRun();
    void AddInfo(const PluginInfo& info, const std::string& state, bool bypassed);
    void AddPath(const wxString& path);
    void OpenSlot(int index);
    void CloseSlotWindows(Plugin* plugin);
    void Remember();
    void PlaceWindow();
    void NoteWindowRect();
    void ApplyTopMost(bool on);
    void ApplyBehaviorSettings(bool userChange);
    void SyncTray();
    void HideToTray();
    void RestoreFromTray();
    void RequestExit();
    bool ChooseOutputFile();
    void SetStatus(const wxString& text, bool error);
    void UpdateRun();
    void OnTick();
    void ApplyNvidia(bool showStatus);
    void StartLevelLog();
    void StopLevelLog();
    void WriteLevelLog(const LevelSample* samples, int count);
    void LoadLevelHistoryFile();
    int SelectedInput() const;
    int SelectedOutput() const;
    bool OutputIsVirtual() const;
    bool OutputIsFile() const;

    Engine engine_;
    Scanner scanner_;
    std::shared_ptr<ProcessChain> chain_ = std::make_shared<ProcessChain>();
    Session session_;
    std::vector<DeviceInfo> devices_;
    std::vector<int> inputMap_;
    std::vector<int> outputMap_;
    std::vector<Slot> slots_;
    std::vector<EditorFrame*> editors_;
    std::vector<ParameterFrame*> parameters_;

    DeviceField* inputField_ = nullptr;
    DeviceField* outputField_ = nullptr;
    MeterBar* inputMeter_ = nullptr;
    MeterBar* outputMeter_ = nullptr;
    VuMeter* inputVu_ = nullptr;
    VuMeter* outputVu_ = nullptr;
    LevelHistory* levelHistory_ = nullptr;
    CheckBox* historyFreeze_ = nullptr;
    CheckBox* noiseBox_ = nullptr;
    CheckBox* roomBox_ = nullptr;
    StrengthSlider* strength_ = nullptr;
    wxStaticText* strengthValue_ = nullptr;
    FlatButton* levelRecord_ = nullptr;
    LevelLogWriter levelLog_;
    wxStaticText* hint_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxStaticText* gainLabel_ = nullptr;
    ChainList* chainList_ = nullptr;
    FlatButton* runButton_ = nullptr;
    CheckBox* topMost_ = nullptr;
    ChoiceChips* bufferChips_ = nullptr;
    ChoiceChips* inputMode_ = nullptr;
    GainSlider* gain_ = nullptr;
    wxTimer timer_;
    TrayIcon* tray_ = nullptr;
    bool trayRunning_ = false;
    bool quitting_ = false;
    bool statusError_ = false;
    bool restoring_ = false;
    bool wasScanning_ = false;

    friend class TrayIcon;
};

} // namespace bv
