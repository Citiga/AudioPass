#include "ui/MainFrame.h"

#include "Theme.h"
#include "Utf.h"
#include "Version.h"
#include "ui/AboutDialog.h"
#include "ui/PluginPicker.h"
#include "ui/SettingsDialog.h"

#include <wx/menu.h>
#include <wx/taskbar.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <mutex>

#include <wx/display.h>

#include <windows.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace bv {
namespace {

class PathDrop : public wxFileDropTarget {
public:
    explicit PathDrop(std::function<void(const wxString&)> add) : add_(std::move(add)) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override {
        for (const auto& file : files) {
            add_(file);
        }
        return true;
    }

private:
    std::function<void(const wxString&)> add_;
};

bool TitleBarOnADisplay(const wxRect& frame) {
    if (frame.width < 80 || frame.height < 80 || wxDisplay::GetCount() == 0) {
        return false;
    }
    const wxRect title(frame.x, frame.y, frame.width, 32);
    const unsigned count = wxDisplay::GetCount();
    for (unsigned i = 0; i < count; ++i) {
        const wxRect overlap = wxDisplay(i).GetGeometry().Intersect(title);
        if (overlap.width >= 48 && overlap.height >= 16) {
            return true;
        }
    }
    return false;
}

wxStaticText* Caption(wxWindow* parent, const wxString& text) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(theme::Muted());
    label->SetBackgroundColour(theme::Elevated());
    label->SetFont(theme::Ui(8, true));
    return label;
}

} // namespace

class TrayIcon : public wxTaskBarIcon {
public:
    explicit TrayIcon(MainFrame* frame) : frame_(frame) {
        Bind(wxEVT_TASKBAR_LEFT_DOWN, [this](wxTaskBarIconEvent&) { frame_->RestoreFromTray(); });
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, [this](wxTaskBarIconEvent&) { frame_->RestoreFromTray(); });
        Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
            if (event.GetId() == wxID_EXIT) {
                frame_->RequestExit();
            } else {
                frame_->RestoreFromTray();
            }
        });
    }

    wxMenu* CreatePopupMenu() override {
        auto* menu = new wxMenu();
        menu->Append(wxID_OPEN, "Open AudioPass");
        menu->AppendSeparator();
        menu->Append(wxID_EXIT, "Exit");
        return menu;
    }

private:
    MainFrame* frame_;
};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "AudioPass", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE),
      timer_(this) {
    SetBackgroundColour(theme::Bg());
    SetForegroundColour(theme::Text());
    SetFont(theme::Ui(10));
    SetMinSize(FromDIP(wxSize(560, 640)));
    SetClientSize(FromDIP(wxSize(560, 760)));
    session_ = loadSession();
    Build();
    LoadDevices();
    gain_->SetDb(session_.gainDb);
    gainLabel_->SetLabel(wxString::Format("%+.1f dB", session_.gainDb));
    if (session_.bufferFrames == 128) {
        bufferChips_->SetSelection(0);
    } else if (session_.bufferFrames == 512) {
        bufferChips_->SetSelection(2);
    } else {
        bufferChips_->SetSelection(1);
    }
    engine_.setOutputGainDb(session_.gainDb);
    inputMode_->SetSelection(session_.inputMono ? 1 : 0);
    engine_.setInputMono(session_.inputMono);
    topMost_->SetValue(session_.topMost);
    ApplyTopMost(session_.topMost);
    std::vector<std::wstring> folders = Scanner::defaultFolders();
    folders.insert(folders.end(), session_.extraFolders.begin(), session_.extraFolders.end());
    scanner_.start(folders, {});
    SetDropTarget(new PathDrop([this](const wxString& path) { AddPath(path); }));
    ApplyBehaviorSettings(false);
    timer_.Start(33);
    ApplyDarkChrome(this);
    ApplyAppIcon(this);
    PlaceWindow();
    CallAfter([this] { RestoreChain(); });
}

void MainFrame::PlaceWindow() {
    const wxRect saved(session_.windowX, session_.windowY, session_.windowW, session_.windowH);
    if (session_.hasWindow && TitleBarOnADisplay(saved)) {
        SetSize(saved);
        if (session_.windowMaximized) {
            Maximize(true);
        }
        return;
    }
    SetClientSize(FromDIP(wxSize(560, 760)));
    if (wxDisplay::GetCount() == 0) {
        Centre();
        return;
    }
    const wxRect area = wxDisplay().GetClientArea();
    const wxSize outer = GetSize();
    const int width = std::min(outer.x, area.width);
    const int height = std::min(outer.y, area.height);
    SetSize(area.x + (area.width - width) / 2, area.y + (area.height - height) / 2, width, height);
}

void MainFrame::ApplyTopMost(bool on) {
    long style = GetWindowStyleFlag();
    if (on) {
        style |= wxSTAY_ON_TOP;
    } else {
        style &= ~wxSTAY_ON_TOP;
    }
    SetWindowStyleFlag(style);
    const HWND hwnd = GetHWND();
    if (hwnd) {
        SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void MainFrame::ApplyBehaviorSettings(bool userChange) {
    if (!userChange && session_.startWithWindows) {
        enableStartWithWindows(true);
    }
    SyncTray();
    if (userChange) {
        Remember();
    }
}

void MainFrame::SyncTray() {
    const bool want = session_.minimizeToTray || session_.closeMinimizes;
    if (!want) {
        if (tray_) {
            tray_->RemoveIcon();
            delete tray_;
            tray_ = nullptr;
        }
        trayRunning_ = false;
        if (!IsShown()) {
            RestoreFromTray();
        }
        return;
    }
    if (!tray_) {
        tray_ = new TrayIcon(this);
    }
    trayRunning_ = engine_.running();
    const wxIcon icon = AppIcon(32);
    if (icon.IsOk()) {
        tray_->SetIcon(icon, trayRunning_ ? "AudioPass is running" : "AudioPass");
    }
}

void MainFrame::HideToTray() {
    if (IsShown() && !IsIconized()) {
        session_.windowMaximized = IsMaximized();
        NoteWindowRect();
    }
    SyncTray();
    Show(false);
    Remember();
}

void MainFrame::RestoreFromTray() {
    if (IsIconized()) {
        Iconize(false);
    }
    Show(true);
    Raise();
}

void MainFrame::RequestExit() {
    quitting_ = true;
    CallAfter([this] { Close(true); });
}

void MainFrame::NoteWindowRect() {
    if (!IsShown() || IsIconized() || IsMaximized()) {
        return;
    }
    const wxSize size = GetSize();
    if (size.x < 80 || size.y < 80) {
        return;
    }
    const wxPoint pos = GetPosition();
    session_.windowX = pos.x;
    session_.windowY = pos.y;
    session_.windowW = size.x;
    session_.windowH = size.y;
    session_.hasWindow = true;
}

void MainFrame::Build() {
    auto* header = new wxPanel(this);
    header->SetBackgroundColour(theme::Bg());
    header->Bind(wxEVT_SIZE, [header](wxSizeEvent& event) {
        event.Skip();
        header->Refresh(false);
    });
    auto* title = new wxStaticText(header, wxID_ANY, "AudioPass");
    title->SetForegroundColour(theme::Text());
    title->SetBackgroundColour(theme::Bg());
    title->SetFont(theme::Ui(14, true));
    auto* version = new wxStaticText(header, wxID_ANY, wxString::FromUTF8(kVersion));
    version->SetForegroundColour(theme::Faint());
    version->SetBackgroundColour(theme::Bg());
    version->SetFont(theme::Mono(9));
    auto* settings = new FlatButton(header, "Settings", FlatButton::Kind::Quiet);
    settings->SetToolTip("Tray, close, and startup.");
    settings->onClick = [this] {
        SettingsDialog dialog(this, session_, [this] { ApplyBehaviorSettings(true); });
        dialog.ShowModal();
    };
    auto* about = new FlatButton(header, "About", FlatButton::Kind::Quiet);
    about->SetToolTip("Version and trademarks.");
    about->onClick = [this] {
        AboutDialog dialog(this);
        dialog.ShowModal();
    };
    topMost_ = new CheckBox(header, "On top");
    topMost_->SetToolTip("Keep this window above other windows.");
    topMost_->onChange = [this](bool on) {
        ApplyTopMost(on);
        Remember();
    };
    runButton_ = new FlatButton(header, "Start", FlatButton::Kind::Primary);
    runButton_->onClick = [this] { ToggleRun(); };
    auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    headerSizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->Add(version, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    headerSizer->AddStretchSpacer();
    headerSizer->Add(settings, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    headerSizer->Add(about, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    headerSizer->Add(topMost_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));
    headerSizer->Add(runButton_, 0, wxALIGN_CENTER_VERTICAL);
    header->SetSizer(headerSizer);

    auto* route = new Card(this);
    inputField_ = new DeviceField(route);
    outputField_ = new DeviceField(route);
    inputMeter_ = new MeterBar(route);
    outputMeter_ = new MeterBar(route);
    inputVu_ = new VuMeter(route);
    outputVu_ = new VuMeter(route);
    inputMeter_->SetToolTip("Input peak");
    outputMeter_->SetToolTip("Output peak");
    inputVu_->SetToolTip("Input VU. min and max are the extremes since Start.");
    outputVu_->SetToolTip("Output VU. min and max are the extremes since Start.");
    gain_ = new GainSlider(route);
    gain_->SetBackgroundColour(theme::Elevated());
    gainLabel_ = new wxStaticText(route, wxID_ANY, "+0.0 dB");
    gainLabel_->SetForegroundColour(theme::Text());
    gainLabel_->SetBackgroundColour(theme::Elevated());
    gainLabel_->SetFont(theme::Mono(9));
    hint_ = new wxStaticText(route, wxID_ANY, "Other apps hear this only through a virtual cable.");
    hint_->SetForegroundColour(theme::Warn());
    hint_->SetBackgroundColour(theme::Elevated());
    hint_->SetFont(theme::Ui(8));
    inputMode_ = new ChoiceChips(route, {"Stereo", "Mono"});
    inputMode_->SetBackgroundColour(theme::Elevated());
    auto* inputHead = new wxBoxSizer(wxHORIZONTAL);
    inputHead->Add(Caption(route, "INPUT"), 0, wxALIGN_CENTER_VERTICAL);
    inputHead->AddStretchSpacer();
    inputHead->Add(inputMode_, 0, wxALIGN_CENTER_VERTICAL);
    auto* routeInner = new wxBoxSizer(wxVERTICAL);
    routeInner->Add(inputHead, 0, wxEXPAND);
    routeInner->Add(inputField_, 0, wxEXPAND | wxTOP, FromDIP(6));
    routeInner->Add(inputMeter_, 0, wxEXPAND | wxTOP, FromDIP(8));
    routeInner->Add(inputVu_, 0, wxEXPAND | wxTOP, FromDIP(8));
    routeInner->Add(Caption(route, "OUTPUT"), 0, wxTOP, FromDIP(16));
    routeInner->Add(outputField_, 0, wxEXPAND | wxTOP, FromDIP(6));
    routeInner->Add(outputMeter_, 0, wxEXPAND | wxTOP, FromDIP(8));
    routeInner->Add(outputVu_, 0, wxEXPAND | wxTOP, FromDIP(8));
    auto* gainHead = new wxBoxSizer(wxHORIZONTAL);
    gainHead->Add(Caption(route, "GAIN"), 0, wxALIGN_CENTER_VERTICAL);
    gainHead->AddStretchSpacer();
    gainHead->Add(gainLabel_, 0, wxALIGN_CENTER_VERTICAL);
    routeInner->Add(gainHead, 0, wxEXPAND | wxTOP, FromDIP(14));
    routeInner->Add(gain_, 0, wxEXPAND | wxTOP, FromDIP(4));
    routeInner->Add(hint_, 0, wxEXPAND | wxTOP, FromDIP(10));
    auto* routePad = new wxBoxSizer(wxVERTICAL);
    routePad->Add(routeInner, 0, wxEXPAND | wxALL, FromDIP(14));
    route->SetSizer(routePad);

    auto* chainCard = new Card(this);
    auto* chainLabel = Caption(chainCard, "CHAIN");
    auto* add = new FlatButton(chainCard, "Add", FlatButton::Kind::Quiet);
    add->onClick = [this] {
        std::vector<std::wstring> folders = Scanner::defaultFolders();
        folders.insert(folders.end(), session_.extraFolders.begin(), session_.extraFolders.end());
        PluginPicker picker(this, scanner_, folders);
        picker.onAddFolder = [this](const std::wstring& folder) {
            session_.extraFolders.push_back(folder);
            Remember();
        };
        if (picker.ShowModal() == wxID_OK) {
            if (const PluginInfo* info = picker.GetSelection()) {
                AddInfo(*info, {}, false);
            }
        }
    };
    chainList_ = new ChainList(chainCard);
    auto* chainHead = new wxBoxSizer(wxHORIZONTAL);
    chainHead->Add(chainLabel, 0, wxALIGN_CENTER_VERTICAL);
    chainHead->AddStretchSpacer();
    chainHead->Add(add, 0);
    auto* chainInner = new wxBoxSizer(wxVERTICAL);
    chainInner->Add(chainHead, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    chainInner->Add(chainList_, 1, wxEXPAND | wxALL, FromDIP(8));
    chainCard->SetSizer(chainInner);

    auto* footer = new wxPanel(this);
    footer->SetBackgroundColour(theme::Bg());
    footer->Bind(wxEVT_SIZE, [footer](wxSizeEvent& event) {
        event.Skip();
        footer->Refresh(false);
    });
    status_ = new wxStaticText(footer, wxID_ANY, "Stopped");
    status_->SetForegroundColour(theme::Muted());
    status_->SetBackgroundColour(theme::Bg());
    status_->SetFont(theme::Mono(8));
    bufferChips_ = new ChoiceChips(footer, {"128", "256", "512"});
    bufferChips_->SetBackgroundColour(theme::Bg());
    auto* footerSizer = new wxBoxSizer(wxHORIZONTAL);
    footerSizer->Add(status_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    footerSizer->Add(bufferChips_, 0, wxALIGN_CENTER_VERTICAL);
    footer->SetSizer(footerSizer);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    root->Add(route, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    root->Add(chainCard, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    root->Add(footer, 0, wxEXPAND | wxALL, FromDIP(16));
    SetSizer(root);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        NoteWindowRect();
        Refresh(false);
    });
    Bind(wxEVT_MOVE, [this](wxMoveEvent& event) {
        event.Skip();
        NoteWindowRect();
    });

    inputMode_->onChange = [this](int index) {
        engine_.setInputMono(index == 1);
        Remember();
    };
    inputField_->onSelect = [this](int) {
        UpdateRun();
        Remember();
        if (engine_.running()) {
            ToggleRun();
            ToggleRun();
        }
    };
    outputField_->onSelect = [this](int) {
        const int index = SelectedOutput();
        if (index >= 0 && devices_[static_cast<size_t>(index)].fileOutput) {
            if (!ChooseOutputFile()) {
                if (session_.outputFile.empty()) {
                    session_.outputId.clear();
                    LoadDevices();
                    return;
                }
            } else {
                LoadDevices();
            }
        }
        UpdateRun();
        Remember();
        if (engine_.running()) {
            ToggleRun();
            ToggleRun();
        }
    };
    bufferChips_->onChange = [this](int) {
        Remember();
        if (engine_.running()) {
            ToggleRun();
            ToggleRun();
        }
    };
    gain_->onChange = [this](float db) {
        gainLabel_->SetLabel(wxString::Format("%+.1f dB", db));
        engine_.setOutputGainDb(db);
        Layout();
        Remember();
    };
    chainList_->onToggleBypass = [this](int index) {
        if (index < 0 || index >= static_cast<int>(slots_.size())) {
            return;
        }
        const bool bypass = !slots_[static_cast<size_t>(index)].plugin->bypassed();
        slots_[static_cast<size_t>(index)].plugin->setBypassed(bypass);
        RefreshChain();
        Remember();
    };
    chainList_->onOpen = [this](int index) { OpenSlot(index); };
    chainList_->onRemove = [this](int index) {
        if (index < 0 || index >= static_cast<int>(slots_.size())) {
            return;
        }
        Plugin* plugin = slots_[static_cast<size_t>(index)].plugin.get();
        CloseSlotWindows(plugin);
        EditChain([this, index] { slots_.erase(slots_.begin() + index); });
        Remember();
    };
    chainList_->onMove = [this](int from, int to) {
        if (from < 0 || to < 0 || from == to || from + 1 == to) {
            return;
        }
        EditChain([this, from, to] {
            Slot slot = std::move(slots_[static_cast<size_t>(from)]);
            slots_.erase(slots_.begin() + from);
            int dest = to;
            if (dest > from) {
                --dest;
            }
            dest = std::clamp(dest, 0, static_cast<int>(slots_.size()));
            slots_.insert(slots_.begin() + dest, std::move(slot));
        });
        Remember();
    };
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnTick(); });
    Bind(wxEVT_ICONIZE, [this](wxIconizeEvent& event) {
        event.Skip();
        if (event.IsIconized() && session_.minimizeToTray) {
            CallAfter([this] {
                if (session_.minimizeToTray && !quitting_) {
                    HideToTray();
                }
            });
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (!quitting_ && session_.closeMinimizes && event.CanVeto()) {
            event.Veto();
            if (session_.minimizeToTray) {
                HideToTray();
            } else {
                Iconize(true);
            }
            return;
        }
        timer_.Stop();
        scanner_.shutdown();
        engine_.stop();
        for (auto& slot : slots_) {
            if (slot.plugin) {
                slot.state = slot.plugin->saveState();
            }
        }
        Remember();
        while (!slots_.empty()) {
            CloseSlotWindows(slots_.back().plugin.get());
            slots_.pop_back();
        }
        if (tray_) {
            tray_->RemoveIcon();
            delete tray_;
            tray_ = nullptr;
        }
        event.Skip();
    });
}

void MainFrame::LoadDevices() {
    devices_ = enumerateDevices();
    inputMap_.clear();
    outputMap_.clear();
    std::vector<wxString> inputs;
    std::vector<wxString> outputs;
    int inputSel = -1;
    int outputSel = -1;
    int defaultIn = -1;
    int firstVirtual = -1;
    for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
        const auto& device = devices_[static_cast<size_t>(i)];
        if (device.capture) {
            inputMap_.push_back(i);
            inputs.push_back(wxString(device.name.c_str()));
            if (device.id == session_.inputId) {
                inputSel = static_cast<int>(inputs.size()) - 1;
            }
            if (device.isDefault) {
                defaultIn = static_cast<int>(inputs.size()) - 1;
            }
        } else {
            outputMap_.push_back(i);
            wxString label(device.name.c_str());
            if (device.fileOutput && !session_.outputFile.empty()) {
                label += L"  \u2192  ";
                label += wxString(std::filesystem::path(session_.outputFile).filename().wstring());
            } else if (device.virtualDevice) {
                label += L"  \u2014  virtual";
            }
            outputs.push_back(label);
            if (device.id == session_.outputId) {
                outputSel = static_cast<int>(outputs.size()) - 1;
            }
            if (firstVirtual < 0 && device.virtualDevice) {
                firstVirtual = static_cast<int>(outputs.size()) - 1;
            }
        }
    }
    if (inputSel < 0) {
        inputSel = defaultIn >= 0 ? defaultIn : (inputs.empty() ? -1 : 0);
    }
    if (outputSel < 0) {
        outputSel = firstVirtual;
    }
    inputField_->SetItems(std::move(inputs), inputSel);
    outputField_->SetItems(std::move(outputs), outputSel);
    UpdateRun();
}

void MainFrame::RestoreChain() {
    restoring_ = true;
    const auto saved = session_.slots;
    for (const auto& slot : saved) {
        AddInfo(slot.info, slot.state, slot.bypassed);
    }
    restoring_ = false;
    Remember();
    RefreshChain();
    if (session_.activateOnStart && !engine_.running()) {
        ToggleRun();
    }
}

void MainFrame::RefreshChain() {
    std::vector<ChainRow> rows;
    rows.reserve(slots_.size());
    for (const auto& slot : slots_) {
        ChainRow row;
        row.title = wxString::FromUTF8(slot.plugin->info().name);
        const std::string vendor = slot.plugin->info().vendor;
        const char* format = slot.plugin->info().format == PluginFormat::Vst3 ? "VST3" : "VST2";
        row.meta = vendor.empty() ? wxString(format) : wxString::FromUTF8(vendor) + L"  \u2014  " + format;
        if (slot.plugin->bypassed()) {
            row.meta += L"  \u2014  bypassed";
        }
        row.bypassed = slot.plugin->bypassed();
        row.hasEditor = slot.plugin->hasEditor() || slot.plugin->parameterCount() > 0;
        rows.push_back(std::move(row));
    }
    chainList_->SetRows(std::move(rows));
}

void MainFrame::SyncChain() {
    std::vector<Plugin*> plugins;
    plugins.reserve(slots_.size());
    for (auto& slot : slots_) {
        plugins.push_back(slot.plugin.get());
    }
    chain_->setPlugins(std::move(plugins));
    RefreshChain();
}

void MainFrame::ToggleRun() {
    if (engine_.running()) {
        engine_.stop();
        runButton_->SetLabel("Start");
        UpdateRun();
        SetStatus("Stopped", false);
        return;
    }
    if (SelectedInput() < 0 || SelectedOutput() < 0) {
        SetStatus("Choose an input and an output.", true);
        return;
    }
    EngineConfig config;
    config.inputId = devices_[static_cast<size_t>(SelectedInput())].id;
    config.outputId = devices_[static_cast<size_t>(SelectedOutput())].id;
    if (OutputIsFile()) {
        if (session_.outputFile.empty()) {
            SetStatus("Choose a WAV file.", true);
            return;
        }
        config.outputPath = session_.outputFile;
    }
    const int bufferIndex = bufferChips_->GetSelection();
    config.bufferFrames = bufferIndex == 0 ? 128 : bufferIndex == 2 ? 512 : 256;
    std::string error;
    if (!engine_.start(config, chain_, error)) {
        SetStatus(wxString::FromUTF8(error), true);
        runButton_->SetLabel("Start");
        UpdateRun();
        return;
    }
    const double rate = engine_.status().sampleRate;
    for (auto& slot : slots_) {
        slot.plugin->prepare(rate > 0 ? rate : 48000.0, 1024);
    }
    engine_.setHold(false);
    inputVu_->ResetSession();
    outputVu_->ResetSession();
    runButton_->SetLabel("Stop");
    runButton_->Enable(true);
    statusError_ = false;
    OnTick();
}

void MainFrame::AddInfo(const PluginInfo& info, const std::string& state, bool bypassed) {
    std::string error;
    auto plugin = loadPlugin(info, error);
    if (!plugin) {
        SetStatus(wxString::FromUTF8(error.empty() ? "Could not load plugin." : error), true);
        return;
    }
    if (!state.empty()) {
        plugin->restoreState(state);
    }
    const double liveRate = engine_.status().sampleRate;
    plugin->prepare(liveRate > 0 ? liveRate : 48000.0, 1024);
    plugin->setBypassed(bypassed);
    EditChain([this, plugin = std::move(plugin), state]() mutable {
        Slot slot;
        slot.plugin = std::move(plugin);
        slot.state = state;
        slots_.push_back(std::move(slot));
    });
    SetStatus(wxString::FromUTF8(slots_.back().plugin->info().name) + " loaded", false);
    if (!restoring_) {
        Remember();
    }
}

void MainFrame::AddPath(const wxString& path) {
    const std::wstring wide = path.ToStdWstring();
    std::wstring ext = std::filesystem::path(wide).extension().wstring();
    for (wchar_t& ch : ext) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    const bool bundle = ext == L".vst3" || std::filesystem::is_directory(wide);
    std::string error;
    PluginInfo info;
    bool found = false;
    {
        std::lock_guard lock(pluginLoadMutex());
        if (bundle) {
        std::vector<Vst3ClassInfo> classes;
        if (!describeVst3File(wide, classes, error)) {
            SetStatus(wxString::FromUTF8(error.empty() ? "That VST3 could not be read." : error), true);
            return;
        }
        for (const auto& item : classes) {
            if (!item.effect) {
                continue;
            }
            info.format = PluginFormat::Vst3;
            info.path = narrow(wide);
            info.uid = item.uid;
            info.name = item.name;
            info.vendor = item.vendor;
            found = true;
            break;
        }
        if (!found) {
            SetStatus("No effect was found in that VST3.", true);
            return;
        }
        } else {
            std::string name;
            std::string vendor;
            if (!describeVst2File(wide, name, vendor)) {
                SetStatus("That file is not a VST2 plugin.", true);
                return;
            }
            info.format = PluginFormat::Vst2;
            info.path = narrow(wide);
            info.name = name;
            info.vendor = vendor;
            found = true;
        }
    }
    if (found) {
        AddInfo(info, {}, false);
    }
}

void MainFrame::OpenSlot(int index) {
    if (index < 0 || index >= static_cast<int>(slots_.size())) {
        return;
    }
    Plugin* plugin = slots_[static_cast<size_t>(index)].plugin.get();
    for (auto* editor : editors_) {
        if (editor->GetPlugin() == plugin) {
            editor->Raise();
            return;
        }
    }
    for (auto* parameter : parameters_) {
        if (parameter->GetPlugin() == plugin) {
            parameter->Raise();
            return;
        }
    }
    const bool resizable = plugin->info().format == PluginFormat::Vst3;
    auto* editor = new EditorFrame(this, plugin, resizable);
    if (editor->Opened()) {
        editor->onClosed = [this, editor] {
            std::erase(editors_, editor);
        };
        editors_.push_back(editor);
        return;
    }
    editor->Destroy();
    if (plugin->parameterCount() <= 0) {
        SetStatus("This plugin has no editor.", true);
        return;
    }
    auto* parameter = new ParameterFrame(this, plugin);
    parameter->onClosed = [this, parameter] { std::erase(parameters_, parameter); };
    parameters_.push_back(parameter);
}

void MainFrame::CloseSlotWindows(Plugin* plugin) {
    for (auto it = editors_.begin(); it != editors_.end();) {
        if ((*it)->GetPlugin() == plugin) {
            EditorFrame* editor = *it;
            it = editors_.erase(it);
            editor->onClosed = nullptr;
            editor->Detach();
            plugin->closeEditor();
            editor->Destroy();
        } else {
            ++it;
        }
    }
    for (auto it = parameters_.begin(); it != parameters_.end();) {
        if ((*it)->GetPlugin() == plugin) {
            ParameterFrame* frame = *it;
            it = parameters_.erase(it);
            frame->onClosed = nullptr;
            frame->Detach();
            frame->Destroy();
        } else {
            ++it;
        }
    }
}

void MainFrame::Remember() {
    session_.inputId.clear();
    session_.outputId.clear();
    if (SelectedInput() >= 0) {
        session_.inputId = devices_[static_cast<size_t>(SelectedInput())].id;
    }
    if (SelectedOutput() >= 0) {
        session_.outputId = devices_[static_cast<size_t>(SelectedOutput())].id;
    }
    const int bufferIndex = bufferChips_->GetSelection();
    session_.bufferFrames = bufferIndex == 0 ? 128 : bufferIndex == 2 ? 512 : 256;
    session_.gainDb = gain_->GetDb();
    session_.inputMono = inputMode_->GetSelection() == 1;
    session_.topMost = topMost_->GetValue();
    if (IsShown() && !IsIconized()) {
        session_.windowMaximized = IsMaximized();
        NoteWindowRect();
    }
    session_.slots.clear();
    for (const auto& slot : slots_) {
        SessionSlot saved;
        saved.info = slot.plugin->info();
        saved.bypassed = slot.plugin->bypassed();
        saved.state = slot.state;
        session_.slots.push_back(std::move(saved));
    }
    saveSession(session_);
}

void MainFrame::SetStatus(const wxString& text, bool error) {
    if (status_->GetLabel() == text && statusError_ == error) {
        return;
    }
    statusError_ = error;
    status_->SetForegroundColour(error ? theme::Danger() : theme::Muted());
    status_->SetLabel(text);
}

bool MainFrame::ChooseOutputFile() {
    std::filesystem::path current(session_.outputFile);
    const wxString directory = current.has_parent_path() ? wxString(current.parent_path().wstring())
                                                         : wxString(configDirectory());
    const wxString name = current.has_filename() ? wxString(current.filename().wstring()) : wxString("AudioPass.wav");
    wxFileDialog dialog(this, "WAV output", directory, name, "WAV files (*.wav)|*.wav",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return false;
    }
    session_.outputFile = dialog.GetPath().ToStdWstring();
    session_.outputId = kFileOutputId;
    return true;
}

void MainFrame::UpdateRun() {
    const bool ready = SelectedInput() >= 0 && SelectedOutput() >= 0 &&
                       (!OutputIsFile() || !session_.outputFile.empty());
    runButton_->Enable(engine_.running() || ready);
    if (OutputIsFile()) {
        hint_->SetLabel("Records a WAV file. Other apps will not hear this output.");
        hint_->Show(true);
    } else {
        hint_->SetLabel("Other apps hear this only through a virtual cable.");
        hint_->Show(!OutputIsVirtual());
    }
    Layout();
}

void MainFrame::OnTick() {
    const std::string fault = engine_.takeFault();
    if (!fault.empty()) {
        engine_.stop();
        runButton_->SetLabel("Start");
        UpdateRun();
        SetStatus(wxString::FromUTF8(fault), true);
    }
    if (tray_ && trayRunning_ != engine_.running()) {
        SyncTray();
    }
    const float inputPeak = engine_.takeInputPeak();
    const float outputPeak = engine_.takeOutputPeak();
    inputMeter_->SetLevel(inputPeak);
    outputMeter_->SetLevel(outputPeak);
    inputVu_->SetLevel(inputPeak);
    outputVu_->SetLevel(outputPeak);
    if (statusError_) {
        return;
    }
    if (engine_.running()) {
        const EngineStatus status = engine_.status();
        SetStatus(wxString::Format(L"%u Hz  \u2014  %u samples  \u2014  %.1f ms", status.sampleRate, status.bufferFrames,
                                   status.latencyMs),
                  false);
        return;
    }
    if (scanner_.scanning()) {
        wasScanning_ = true;
        SetStatus(wxString::Format("Indexing plugins…  %zu", scanner_.plugins().size()), false);
        return;
    }
    if (wasScanning_) {
        wasScanning_ = false;
        SetStatus("Stopped", false);
    }
}

int MainFrame::SelectedInput() const {
    const int index = inputField_->GetSelection();
    if (index < 0 || index >= static_cast<int>(inputMap_.size())) {
        return -1;
    }
    return inputMap_[static_cast<size_t>(index)];
}

int MainFrame::SelectedOutput() const {
    const int index = outputField_->GetSelection();
    if (index < 0 || index >= static_cast<int>(outputMap_.size())) {
        return -1;
    }
    return outputMap_[static_cast<size_t>(index)];
}

bool MainFrame::OutputIsVirtual() const {
    const int index = SelectedOutput();
    return index >= 0 && devices_[static_cast<size_t>(index)].virtualDevice;
}

bool MainFrame::OutputIsFile() const {
    const int index = SelectedOutput();
    return index >= 0 && devices_[static_cast<size_t>(index)].fileOutput;
}

} // namespace bv
