#include "plugins/Plugin.h"

#include "Utf.h"
#include "plugins/Vst2Abi.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace bv {
namespace {

std::mutex gMapMu;
std::unordered_map<vst2::Effect*, class Vst2Plugin*> gPlugins;
thread_local class Vst2Plugin* tCurrent = nullptr;
thread_local int tLevel = 1;

class Vst2Plugin;

intptr_t __cdecl masterCallback(vst2::Effect* effect, int32_t opcode, int32_t index, intptr_t value,
                                void* ptr, float opt);

class Vst2Plugin final : public Plugin {
public:
    Vst2Plugin(PluginInfo info, HMODULE module, vst2::Effect* effect)
        : info_(std::move(info)), module_(module), effect_(effect) {
        std::lock_guard lock(gMapMu);
        gPlugins[effect_] = this;
    }

    ~Vst2Plugin() override {
        closeEditor();
        if (effect_) {
            if (started_) {
                dispatch(vst2::effStopProcess);
            }
            if (active_) {
                dispatch(vst2::effMainsChanged, 0, 0);
            }
            if (opened_) {
                dispatch(vst2::effClose);
            }
            std::lock_guard lock(gMapMu);
            gPlugins.erase(effect_);
            effect_ = nullptr;
        }
        if (module_) {
            FreeLibrary(module_);
        }
    }

    const PluginInfo& info() const override { return info_; }

    void prepare(double sampleRate, int maxBlock) override {
        if (!effect_) {
            return;
        }
        if (!opened_) {
            dispatch(vst2::effOpen);
            opened_ = true;
            if (info_.name.empty()) {
                char buffer[128]{};
                dispatch(vst2::effGetEffectName, 0, 0, buffer);
                buffer[127] = '\0';
                if (buffer[0] == '\0') {
                    dispatch(vst2::effGetProductString, 0, 0, buffer);
                    buffer[127] = '\0';
                }
                info_.name = buffer[0] != '\0' ? buffer : "VST2 plugin";
            }
            version_ = static_cast<int>(dispatch(vst2::effGetVstVersion));
            if (version_ <= 0) {
                version_ = 2400;
            }
        }
        if (active_) {
            if (version_ >= 2400 && started_) {
                dispatch(vst2::effStopProcess);
                started_ = false;
            }
            dispatch(vst2::effMainsChanged, 0, 0);
            active_ = false;
        }
        sampleRate_ = sampleRate;
        maxBlock_ = std::max(32, maxBlock);
        dispatch(vst2::effSetSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate));
        dispatch(vst2::effSetBlockSize, 0, maxBlock_);
        if (!deferred_.empty()) {
            const std::string pending = std::move(deferred_);
            deferred_.clear();
            applyState(pending);
        }
        dispatch(vst2::effMainsChanged, 0, 1);
        active_ = true;
        if (version_ >= 2400) {
            dispatch(vst2::effStartProcess);
            started_ = true;
        }
        time_.sampleRate = sampleRate;
        time_.tempo = 120.0;
        time_.timeSigNumerator = 4;
        time_.timeSigDenominator = 4;
        const int inputs = effect_->numInputs;
        const int outputs = effect_->numOutputs;
        mode_ = inputs <= 0 ? Mode::Generator : inputs == 1 ? Mode::Mono : Mode::Stereo;
        outMode_ = outputs <= 1 ? Mode::Mono : Mode::Stereo;
        const size_t samples = static_cast<size_t>(maxBlock_);
        inL_.assign(samples, 0.f);
        inR_.assign(samples, 0.f);
        outL_.assign(samples, 0.f);
        outR_.assign(samples, 0.f);
    }

    void process(float** stereo, int frames) override {
        if (!effect_ || !effect_->processReplacing || !active_ || frames <= 0) {
            return;
        }
        int done = 0;
        while (done < frames) {
            const int n = std::min(frames - done, maxBlock_);
            processBlock(stereo[0] + done, stereo[1] + done, n);
            done += n;
        }
    }

    void setBypassed(bool bypassed) override { bypassed_.store(bypassed, std::memory_order_relaxed); }
    bool bypassed() const override { return bypassed_.load(std::memory_order_relaxed); }
    bool hasEditor() const override {
        return effect_ && (effect_->flags & vst2::kHasEditor) != 0;
    }

    int parameterCount() const override { return effect_ ? effect_->numParams : 0; }

    std::string parameterName(int index) const override {
        char buffer[64]{};
        dispatch(vst2::effGetParamName, index, 0, buffer);
        buffer[63] = '\0';
        if (buffer[0] == '\0') {
            return "Parameter " + std::to_string(index + 1);
        }
        return buffer;
    }

    float parameterValue(int index) const override {
        if (!effect_ || !effect_->getParameter || index < 0 || index >= effect_->numParams) {
            return 0.f;
        }
        return effect_->getParameter(effect_, index);
    }

    void setParameterValue(int index, float normalized) override {
        if (!effect_ || !effect_->setParameter || index < 0 || index >= effect_->numParams) {
            return;
        }
        effect_->setParameter(effect_, index, std::clamp(normalized, 0.f, 1.f));
    }

    bool openEditor(void* parentHwnd, int& width, int& height,
                    std::function<void(int, int)> resize) override {
        closeEditor();
        if (!hasEditor() || !parentHwnd) {
            return false;
        }
        resize_ = std::move(resize);
        vst2::Rect* rect = nullptr;
        dispatch(vst2::effEditGetRect, 0, 0, &rect);
        width = 400;
        height = 300;
        if (rect) {
            width = std::max(80, static_cast<int>(rect->right - rect->left));
            height = std::max(60, static_cast<int>(rect->bottom - rect->top));
        }
        dispatch(vst2::effEditOpen, 0, 0, parentHwnd);
        editorOpen_ = true;
        rect = nullptr;
        dispatch(vst2::effEditGetRect, 0, 0, &rect);
        if (rect) {
            width = std::max(80, static_cast<int>(rect->right - rect->left));
            height = std::max(60, static_cast<int>(rect->bottom - rect->top));
        }
        return true;
    }

    void closeEditor() override {
        if (editorOpen_ && effect_) {
            dispatch(vst2::effEditClose);
        }
        editorOpen_ = false;
        resize_ = nullptr;
    }

    void idleEditor() override {
        if (editorOpen_) {
            dispatch(vst2::effEditIdle);
        }
    }

    std::string saveState() const override {
        if (!effect_) {
            return {};
        }
        std::string blob;
        if ((effect_->flags & vst2::kProgramChunks) != 0) {
            void* chunk = nullptr;
            const intptr_t size = dispatch(vst2::effGetChunk, 0, 0, &chunk);
            if (size > 0 && chunk && size < 8 * 1024 * 1024) {
                blob.push_back(1);
                blob.append(static_cast<char*>(chunk), static_cast<size_t>(size));
                return blob;
            }
        }
        blob.push_back(2);
        const int count = effect_->numParams;
        blob.append(reinterpret_cast<const char*>(&count), sizeof(count));
        for (int i = 0; i < count; ++i) {
            const float value = parameterValue(i);
            blob.append(reinterpret_cast<const char*>(&value), sizeof(value));
        }
        return blob;
    }

    void restoreState(std::string_view data) override {
        if (!effect_ || data.size() < 2) {
            return;
        }
        if (!opened_) {
            deferred_.assign(data.begin(), data.end());
            return;
        }
        const bool wasActive = active_;
        if (wasActive) {
            if (started_) {
                dispatch(vst2::effStopProcess);
                started_ = false;
            }
            dispatch(vst2::effMainsChanged, 0, 0);
            active_ = false;
        }
        applyState(data);
        if (wasActive) {
            dispatch(vst2::effMainsChanged, 0, 1);
            active_ = true;
            if (version_ >= 2400) {
                dispatch(vst2::effStartProcess);
                started_ = true;
            }
        }
    }

    void applyState(std::string_view data) {
        if (data.size() < 2) {
            return;
        }
        if (data[0] == 1 && (effect_->flags & vst2::kProgramChunks) != 0) {
            dispatch(vst2::effSetChunk, 0, static_cast<intptr_t>(data.size() - 1),
                     const_cast<char*>(data.data() + 1));
            return;
        }
        if (data[0] == 2 && data.size() >= 1 + sizeof(int)) {
            int count = 0;
            std::memcpy(&count, data.data() + 1, sizeof(count));
            size_t offset = 1 + sizeof(int);
            count = std::min(count, effect_->numParams);
            for (int i = 0; i < count; ++i) {
                if (offset + sizeof(float) > data.size()) {
                    break;
                }
                float value = 0.f;
                std::memcpy(&value, data.data() + offset, sizeof(value));
                offset += sizeof(float);
                setParameterValue(i, value);
            }
        }
    }

    void onSizeWindow(int width, int height) {
        if (resize_) {
            resize_(width, height);
        }
    }

    vst2::TimeInfo& time() { return time_; }
    double sampleRate() const { return sampleRate_; }
    int maxBlock() const { return maxBlock_; }
    std::atomic<int64_t>& samplePos() { return samplePos_; }

    intptr_t dispatch(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr,
                      float opt = 0.f) const {
        if (!effect_ || !effect_->dispatcher) {
            return 0;
        }
        Vst2Plugin* self = const_cast<Vst2Plugin*>(this);
        Vst2Plugin* previous = tCurrent;
        tCurrent = self;
        const intptr_t result = effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
        tCurrent = previous;
        return result;
    }

private:
    enum class Mode { Mono, Stereo, Generator };

    void processBlock(float* left, float* right, int frames) {
        for (int i = 0; i < frames; ++i) {
            inL_[static_cast<size_t>(i)] = left[i];
            inR_[static_cast<size_t>(i)] = right[i];
            outL_[static_cast<size_t>(i)] = 0.f;
            outR_[static_cast<size_t>(i)] = 0.f;
        }
        float* inputs[2] = {inL_.data(), inR_.data()};
        float* outputs[2] = {outL_.data(), outR_.data()};
        if (mode_ == Mode::Mono) {
            for (int i = 0; i < frames; ++i) {
                inL_[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
            }
        }
        const int previous = tLevel;
        tLevel = 2;
        Vst2Plugin* previousPlugin = tCurrent;
        tCurrent = this;
        const int64_t pos = samplePos_.fetch_add(frames);
        time_.samplePos = static_cast<double>(pos);
        time_.sampleRate = sampleRate_;
        time_.nanoSeconds = time_.samplePos / std::max(1.0, sampleRate_) * 1.0e9;
        time_.ppqPos = time_.samplePos / std::max(1.0, sampleRate_) * time_.tempo / 60.0;
        time_.flags = vst2::kTransportPlaying | vst2::kNanosValid | vst2::kPpqPosValid |
                      vst2::kTempoValid | vst2::kTimeSigValid;
        effect_->processReplacing(effect_, inputs, outputs, frames);
        tCurrent = previousPlugin;
        tLevel = previous;
        for (int i = 0; i < frames; ++i) {
            left[i] = outL_[static_cast<size_t>(i)];
            right[i] = outMode_ == Mode::Mono ? outL_[static_cast<size_t>(i)]
                                              : outR_[static_cast<size_t>(i)];
        }
    }

    PluginInfo info_;
    HMODULE module_ = nullptr;
    vst2::Effect* effect_ = nullptr;
    bool opened_ = false;
    bool active_ = false;
    bool started_ = false;
    bool editorOpen_ = false;
    int version_ = 0;
    int maxBlock_ = 1024;
    double sampleRate_ = 48000;
    Mode mode_ = Mode::Stereo;
    Mode outMode_ = Mode::Stereo;
    std::atomic<bool> bypassed_{false};
    std::atomic<int64_t> samplePos_{0};
    vst2::TimeInfo time_{};
    std::vector<float> inL_, inR_, outL_, outR_;
    std::function<void(int, int)> resize_;
    std::string deferred_;
};

intptr_t __cdecl masterCallback(vst2::Effect* effect, int32_t opcode, int32_t, intptr_t,
                                void* ptr, float) {
    Vst2Plugin* plugin = tCurrent;
    if (!plugin && effect) {
        std::lock_guard lock(gMapMu);
        const auto it = gPlugins.find(effect);
        if (it != gPlugins.end()) {
            plugin = it->second;
        }
    }
    using namespace vst2;
    switch (opcode) {
    case masterVersion:
        return 2400;
    case masterCurrentId:
        return 0;
    case masterGetSampleRate:
        return plugin ? static_cast<intptr_t>(plugin->sampleRate()) : 48000;
    case masterGetBlockSize:
        return plugin ? plugin->maxBlock() : 1024;
    case masterGetInputLatency:
    case masterGetOutputLatency:
        return 0;
    case masterGetCurrentProcessLevel:
        return tLevel;
    case masterGetAutomationState:
        return 0;
    case masterGetVendorString:
    case masterGetProductString:
        if (ptr) {
            std::strncpy(static_cast<char*>(ptr), "AudioPass", 63);
        }
        return 1;
    case masterGetVendorVersion:
        return 100;
    case masterCanDo: {
        if (!ptr) {
            return 0;
        }
        const std::string can(static_cast<const char*>(ptr));
        if (can == "sizeWindow" || can == "sendVstTimeInfo" || can == "sendVstEvents" ||
            can == "sendVstMidiEvent" || can == "receiveVstEvents" || can == "receiveVstMidiEvent" ||
            can == "reportConnectionChanges" || can == "acceptIOChanges" || can == "supplyIdle") {
            return 1;
        }
        return 0;
    }
    case masterGetLanguage:
        return 1;
    case masterGetTime:
        return plugin ? reinterpret_cast<intptr_t>(&plugin->time()) : 0;
    case masterUpdateDisplay:
    case masterIOChanged:
    case masterBeginEdit:
    case masterEndEdit:
    case masterAutomate:
        return 1;
    default:
        return 0;
    }
}

// sizeWindow passes width in `index` and height in `value`.
intptr_t __cdecl masterCallbackFixed(vst2::Effect* effect, int32_t opcode, int32_t index,
                                     intptr_t value, void* ptr, float opt) {
    if (opcode == vst2::masterSizeWindow) {
        Vst2Plugin* plugin = tCurrent;
        if (!plugin && effect) {
            std::lock_guard lock(gMapMu);
            const auto it = gPlugins.find(effect);
            if (it != gPlugins.end()) {
                plugin = it->second;
            }
        }
        if (plugin) {
            plugin->onSizeWindow(index, static_cast<int>(value));
        }
        return 1;
    }
    return masterCallback(effect, opcode, index, value, ptr, opt);
}

HMODULE loadModule(const std::wstring& path) {
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

vst2::MainFn moduleMain(HMODULE module) {
    auto* main = reinterpret_cast<vst2::MainFn>(GetProcAddress(module, "VSTPluginMain"));
    if (!main) {
        main = reinterpret_cast<vst2::MainFn>(GetProcAddress(module, "main"));
    }
    return main;
}

std::string effectText(vst2::Effect* effect, int opcode) {
    char buffer[128]{};
    if (effect && effect->dispatcher) {
        effect->dispatcher(effect, opcode, 0, 0, buffer, 0.f);
        buffer[127] = '\0';
    }
    return buffer;
}

} // namespace

std::mutex& pluginLoadMutex() {
    static std::mutex mutex;
    return mutex;
}

bool describeVst2File(const std::wstring& path, std::string& name, std::string& vendor) {
    HMODULE module = loadModule(path);
    if (!module) {
        return false;
    }
    auto* main = moduleMain(module);
    if (!main) {
        FreeLibrary(module);
        return false;
    }
    tCurrent = nullptr;
    vst2::Effect* effect = main(&masterCallbackFixed);
    if (!effect || effect->magic != vst2::kMagic) {
        FreeLibrary(module);
        return false;
    }
    if (effect->dispatcher) {
        effect->dispatcher(effect, vst2::effOpen, 0, 0, nullptr, 0.f);
    }
    name = effectText(effect, vst2::effGetEffectName);
    vendor = effectText(effect, vst2::effGetVendorString);
    if (name.empty()) {
        name = effectText(effect, vst2::effGetProductString);
    }
    if (name.empty()) {
        name = "VST2 plugin";
    }
    if (effect->dispatcher) {
        effect->dispatcher(effect, vst2::effClose, 0, 0, nullptr, 0.f);
    }
    FreeLibrary(module);
    return true;
}

std::unique_ptr<Plugin> loadVst2Plugin(const PluginInfo& info, std::string& error) {
    const std::wstring path = widen(info.path);
    HMODULE module = loadModule(path);
    if (!module) {
        error = "Could not load the VST2 file.";
        return nullptr;
    }
    auto* main = moduleMain(module);
    if (!main) {
        FreeLibrary(module);
        error = "The file does not export a VST2 entry point.";
        return nullptr;
    }
    tCurrent = nullptr;
    vst2::Effect* effect = main(&masterCallbackFixed);
    if (!effect || effect->magic != vst2::kMagic) {
        FreeLibrary(module);
        error = "The file is not a VST2 plugin.";
        return nullptr;
    }
    return std::make_unique<Vst2Plugin>(std::move(info), module, effect);
}

} // namespace bv
