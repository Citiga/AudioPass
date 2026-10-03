#include "plugins/Plugin.h"

#include "Utf.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/utility/uid.h"

namespace bv {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

class BvHostApp final : public HostApplication {
public:
    tresult PLUGIN_API getName(String128 name) override {
        return StringConvert::convert(std::string("AudioPass"), name) ? kResultOk : kInternalError;
    }
};

class BvComponentHandler final : public IComponentHandler {
public:
    using Sink = std::function<void(ParamID, ParamValue)>;

    explicit BvComponentHandler(Sink sink) : sink_(std::move(sink)) { FUNKNOWN_CTOR }
    ~BvComponentHandler() { FUNKNOWN_DTOR }

    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        if (sink_) {
            sink_(id, value);
        }
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    DECLARE_FUNKNOWN_METHODS
private:
    Sink sink_;
};

IMPLEMENT_FUNKNOWN_METHODS(BvComponentHandler, IComponentHandler, IComponentHandler::iid)

class BvPlugFrame final : public IPlugFrame {
public:
    using Resize = std::function<tresult(ViewRect*)>;

    explicit BvPlugFrame(Resize resize) : resize_(std::move(resize)) { FUNKNOWN_CTOR }
    ~BvPlugFrame() { FUNKNOWN_DTOR }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!newSize || resizing_) {
            return kResultOk;
        }
        resizing_ = true;
        tresult result = kResultOk;
        if (resize_) {
            result = resize_(newSize);
        }
        if (view) {
            view->onSize(newSize);
        }
        resizing_ = false;
        return result;
    }

    DECLARE_FUNKNOWN_METHODS
private:
    Resize resize_;
    bool resizing_ = false;
};

IMPLEMENT_FUNKNOWN_METHODS(BvPlugFrame, IPlugFrame, IPlugFrame::iid)

class MemStream final : public IBStream {
public:
    MemStream() { FUNKNOWN_CTOR }
    explicit MemStream(std::string_view data) : bytes_(data.begin(), data.end()) { FUNKNOWN_CTOR }
    ~MemStream() { FUNKNOWN_DTOR }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        if (numBytes < 0 || (!buffer && numBytes > 0)) {
            return kInvalidArgument;
        }
        const int64 remaining = static_cast<int64>(bytes_.size()) - pos_;
        const int32 copied = static_cast<int32>(std::max<int64>(0, std::min<int64>(numBytes, remaining)));
        if (copied > 0) {
            std::memcpy(buffer, bytes_.data() + pos_, static_cast<size_t>(copied));
            pos_ += copied;
        }
        if (numBytesRead) {
            *numBytesRead = copied;
        }
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (numBytes < 0 || (!buffer && numBytes > 0)) {
            return kInvalidArgument;
        }
        const size_t end = static_cast<size_t>(pos_) + static_cast<size_t>(numBytes);
        if (end > bytes_.size()) {
            bytes_.resize(end);
        }
        if (numBytes > 0) {
            std::memcpy(bytes_.data() + pos_, buffer, static_cast<size_t>(numBytes));
            pos_ += numBytes;
        }
        if (numBytesWritten) {
            *numBytesWritten = numBytes;
        }
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
        int64 next = pos_;
        if (mode == IBStream::kIBSeekSet) {
            next = pos;
        } else if (mode == IBStream::kIBSeekCur) {
            next = pos_ + pos;
        } else if (mode == IBStream::kIBSeekEnd) {
            next = static_cast<int64>(bytes_.size()) + pos;
        } else {
            return kInvalidArgument;
        }
        if (next < 0) {
            next = 0;
        }
        pos_ = next;
        if (result) {
            *result = pos_;
        }
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override {
        if (!pos) {
            return kInvalidArgument;
        }
        *pos = pos_;
        return kResultOk;
    }

    std::string data() const { return std::string(bytes_.begin(), bytes_.end()); }
    void rewind() { pos_ = 0; }

    DECLARE_FUNKNOWN_METHODS
private:
    std::vector<char> bytes_;
    int64 pos_ = 0;
};

IMPLEMENT_FUNKNOWN_METHODS(MemStream, IBStream, IBStream::iid)

FUnknown* hostContext() {
    static IPtr<BvHostApp> host(owned(new BvHostApp()));
    PluginContextFactory::instance().setPluginContext(host);
    return host;
}

bool isMusicalEffect(const VST3::Hosting::ClassInfo& info) {
    bool effect = false;
    bool instrument = false;
    for (const auto& sub : info.subCategories()) {
        if (sub == "Instrument") {
            instrument = true;
        }
        if (sub.rfind("Fx", 0) == 0) {
            effect = true;
        }
    }
    return effect || !instrument;
}

class Vst3Plugin final : public Plugin {
public:
    Vst3Plugin(PluginInfo info, VST3::Hosting::Module::Ptr module, IPtr<PlugProvider> provider)
        : info_(std::move(info)), module_(std::move(module)), provider_(std::move(provider)) {
        component_ = provider_->getComponentPtr();
        processor_ = FUnknownPtr<IAudioProcessor>(component_);
        controller_ = provider_->getControllerPtr();
        handler_ = owned(new BvComponentHandler([this](ParamID id, ParamValue value) {
            queueChange(id, static_cast<float>(value));
        }));
        if (controller_) {
            controller_->setComponentHandler(handler_);
        }
    }

    ~Vst3Plugin() override {
        closeEditor();
        deactivate();
    }

    const PluginInfo& info() const override { return info_; }

    void prepare(double sampleRate, int maxBlock) override {
        if (!processor_ || !component_) {
            return;
        }
        deactivate();
        sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
        maxBlock_ = std::max(32, maxBlock);
        arrangeBuses();
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = maxBlock_;
        setup.sampleRate = sampleRate_;
        processor_->setupProcessing(setup);
        if (!deferred_.empty()) {
            applyState(deferred_);
            deferred_.clear();
        }
        component_->setActive(true);
        processor_->setProcessing(true);
        active_ = true;
        cacheParameters();
        allocateBuffers();
        context_ = {};
        context_.sampleRate = sampleRate_;
        context_.tempo = 120.0;
        context_.timeSigNumerator = 4;
        context_.timeSigDenominator = 4;
        context_.state = ProcessContext::kPlaying | ProcessContext::kSystemTimeValid |
                         ProcessContext::kContTimeValid | ProcessContext::kProjectTimeMusicValid |
                         ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;
    }

    void process(float** stereo, int frames) override {
        if (!active_ || !processor_ || frames <= 0) {
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

    bool hasEditor() const override { return controller_ != nullptr; }

    int parameterCount() const override { return static_cast<int>(params_.size()); }

    std::string parameterName(int index) const override {
        if (index < 0 || index >= static_cast<int>(params_.size())) {
            return {};
        }
        return params_[static_cast<size_t>(index)].name;
    }

    float parameterValue(int index) const override {
        if (!controller_ || index < 0 || index >= static_cast<int>(params_.size())) {
            return 0.f;
        }
        return static_cast<float>(controller_->getParamNormalized(params_[static_cast<size_t>(index)].id));
    }

    void setParameterValue(int index, float normalized) override {
        if (!controller_ || index < 0 || index >= static_cast<int>(params_.size())) {
            return;
        }
        const ParamID id = params_[static_cast<size_t>(index)].id;
        const double value = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
        controller_->setParamNormalized(id, value);
        queueChange(id, static_cast<float>(value));
    }

    bool openEditor(void* parentHwnd, int& width, int& height, std::function<void(int, int)> resize) override {
        closeEditor();
        if (!controller_ || !parentHwnd) {
            return false;
        }
        IPlugView* raw = controller_->createView(ViewType::kEditor);
        if (!raw) {
            return false;
        }
        view_ = owned(raw);
        if (view_->isPlatformTypeSupported(kPlatformTypeHWND) != kResultOk) {
            view_ = nullptr;
            return false;
        }
        ViewRect rect;
        if (view_->getSize(&rect) != kResultOk) {
            rect = ViewRect(0, 0, 400, 300);
        }
        width = std::max(80, static_cast<int>(rect.getWidth()));
        height = std::max(60, static_cast<int>(rect.getHeight()));
        frame_ = owned(new BvPlugFrame([resize = std::move(resize)](ViewRect* next) {
            if (resize && next) {
                resize(next->getWidth(), next->getHeight());
            }
            return kResultOk;
        }));
        view_->setFrame(frame_);
        if (view_->attached(parentHwnd, kPlatformTypeHWND) != kResultOk) {
            view_->setFrame(nullptr);
            view_ = nullptr;
            frame_ = nullptr;
            return false;
        }
        if (view_->getSize(&rect) == kResultOk) {
            width = std::max(80, static_cast<int>(rect.getWidth()));
            height = std::max(60, static_cast<int>(rect.getHeight()));
        }
        resizable_ = view_->canResize() == kResultOk;
        return true;
    }

    bool editorResizable() const override { return resizable_; }

    void resizeEditor(int width, int height) override {
        if (!view_ || width <= 0 || height <= 0) {
            return;
        }
        ViewRect rect(0, 0, width, height);
        view_->onSize(&rect);
    }

    void closeEditor() override {
        if (view_) {
            view_->setFrame(nullptr);
            view_->removed();
            view_ = nullptr;
        }
        frame_ = nullptr;
    }

    void idleEditor() override {}

    std::string saveState() const override {
        if (!component_) {
            return {};
        }
        MemStream componentStream;
        MemStream controllerStream;
        const bool haveComponent = component_->getState(&componentStream) == kResultOk;
        const bool haveController = controller_ && controller_->getState(&controllerStream) == kResultOk;
        if (!haveComponent && !haveController) {
            return {};
        }
        const std::string componentBytes = haveComponent ? componentStream.data() : std::string{};
        const std::string controllerBytes = haveController ? controllerStream.data() : std::string{};
        if (componentBytes.size() + controllerBytes.size() > 8 * 1024 * 1024) {
            return {};
        }
        std::string blob;
        blob.push_back('3');
        const uint32_t componentSize = static_cast<uint32_t>(componentBytes.size());
        const uint32_t controllerSize = static_cast<uint32_t>(controllerBytes.size());
        blob.append(reinterpret_cast<const char*>(&componentSize), sizeof(componentSize));
        blob.append(reinterpret_cast<const char*>(&controllerSize), sizeof(controllerSize));
        blob.append(componentBytes);
        blob.append(controllerBytes);
        return blob;
    }

    void restoreState(std::string_view data) override {
        if (data.size() < 1 + sizeof(uint32_t) * 2 || data[0] != '3') {
            return;
        }
        deferred_.assign(data.begin(), data.end());
        if (active_) {
            const double rate = sampleRate_;
            const int block = maxBlock_;
            prepare(rate, block);
        }
    }

private:
    struct Param {
        ParamID id = 0;
        std::string name;
    };

    void deactivate() {
        if (!active_) {
            return;
        }
        if (processor_) {
            processor_->setProcessing(false);
        }
        if (component_) {
            component_->setActive(false);
        }
        active_ = false;
    }

    void arrangeBuses() {
        const int32 inputs = component_->getBusCount(kAudio, kInput);
        const int32 outputs = component_->getBusCount(kAudio, kOutput);
        for (int32 i = 0; i < inputs; ++i) {
            component_->activateBus(kAudio, kInput, i, i == 0);
        }
        for (int32 i = 0; i < outputs; ++i) {
            component_->activateBus(kAudio, kOutput, i, i == 0);
        }
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        SpeakerArrangement mono = SpeakerArr::kMono;
        const int32 inUsed = inputs > 0 ? 1 : 0;
        const int32 outUsed = outputs > 0 ? 1 : 0;
        SpeakerArrangement inArr = stereo;
        SpeakerArrangement outArr = stereo;
        tresult arranged = kResultFalse;
        if (inUsed + outUsed > 0) {
            arranged = processor_->setBusArrangements(inUsed ? &inArr : nullptr, inUsed,
                                                      outUsed ? &outArr : nullptr, outUsed);
        }
        if (arranged != kResultOk && arranged != kResultTrue) {
            inArr = mono;
            outArr = mono;
            processor_->setBusArrangements(inUsed ? &inArr : nullptr, inUsed, outUsed ? &outArr : nullptr,
                                           outUsed);
        }
        inChannels_ = 0;
        outChannels_ = 0;
        if (inUsed) {
            BusInfo bus{};
            if (component_->getBusInfo(kAudio, kInput, 0, bus) == kResultOk) {
                inChannels_ = std::clamp(bus.channelCount, 0, 8);
            }
        }
        if (outUsed) {
            BusInfo bus{};
            if (component_->getBusInfo(kAudio, kOutput, 0, bus) == kResultOk) {
                outChannels_ = std::clamp(bus.channelCount, 0, 8);
            }
        }
        if (outChannels_ <= 0) {
            outChannels_ = 2;
        }
    }

    void allocateBuffers() {
        const size_t n = static_cast<size_t>(maxBlock_);
        inBuffers_.assign(static_cast<size_t>(std::max(inChannels_, 0)), std::vector<float>(n));
        outBuffers_.assign(static_cast<size_t>(std::max(outChannels_, 1)), std::vector<float>(n));
    }

    void cacheParameters() {
        params_.clear();
        if (!controller_) {
            return;
        }
        const int32 count = controller_->getParameterCount();
        params_.reserve(static_cast<size_t>(std::max(0, count)));
        for (int32 i = 0; i < count; ++i) {
            ParameterInfo info{};
            if (controller_->getParameterInfo(i, info) != kResultOk) {
                continue;
            }
            Param param;
            param.id = info.id;
            param.name = StringConvert::convert(info.title);
            if (param.name.empty()) {
                param.name = "Parameter " + std::to_string(i + 1);
            }
            params_.push_back(std::move(param));
        }
    }

    void applyState(std::string_view data) {
        if (data.size() < 1 + sizeof(uint32_t) * 2 || data[0] != '3' || !component_) {
            return;
        }
        uint32_t componentSize = 0;
        uint32_t controllerSize = 0;
        std::memcpy(&componentSize, data.data() + 1, sizeof(componentSize));
        std::memcpy(&controllerSize, data.data() + 1 + sizeof(uint32_t), sizeof(controllerSize));
        const size_t header = 1 + sizeof(uint32_t) * 2;
        if (header + componentSize + controllerSize > data.size()) {
            return;
        }
        const char* cursor = data.data() + header;
        if (componentSize > 0) {
            MemStream stream(std::string_view(cursor, componentSize));
            component_->setState(&stream);
            if (controller_) {
                stream.rewind();
                controller_->setComponentState(&stream);
            }
        }
        cursor += componentSize;
        if (controller_ && controllerSize > 0) {
            MemStream stream(std::string_view(cursor, controllerSize));
            controller_->setState(&stream);
        }
    }

    void queueChange(ParamID id, float value) {
        std::lock_guard lock(changeMu_);
        changes_.push_back({id, value});
    }

    void processBlock(float* left, float* right, int frames) {
        const size_t n = static_cast<size_t>(frames);
        for (int ch = 0; ch < inChannels_; ++ch) {
            float* dst = inBuffers_[static_cast<size_t>(ch)].data();
            if (inChannels_ == 1) {
                for (int i = 0; i < frames; ++i) {
                    dst[i] = 0.5f * (left[i] + right[i]);
                }
            } else if (ch == 0) {
                std::copy_n(left, n, dst);
            } else if (ch == 1) {
                std::copy_n(right, n, dst);
            } else {
                std::fill_n(dst, n, 0.f);
            }
        }
        for (auto& buffer : outBuffers_) {
            std::fill_n(buffer.data(), n, 0.f);
        }

        float* inPtrs[8] = {};
        float* outPtrs[8] = {};
        for (int ch = 0; ch < inChannels_ && ch < 8; ++ch) {
            inPtrs[ch] = inBuffers_[static_cast<size_t>(ch)].data();
        }
        for (int ch = 0; ch < outChannels_ && ch < 8; ++ch) {
            outPtrs[ch] = outBuffers_[static_cast<size_t>(ch)].data();
        }

        std::vector<Change> local;
        {
            std::lock_guard lock(changeMu_);
            local.swap(changes_);
        }
        ParameterChanges changes(static_cast<int32>(std::max<size_t>(local.size(), 1)));
        for (const Change& change : local) {
            int32 index = 0;
            if (IParamValueQueue* queue = changes.addParameterData(change.id, index)) {
                int32 point = 0;
                queue->addPoint(0, change.value, point);
            }
        }

        AudioBusBuffers input{};
        input.numChannels = inChannels_;
        input.channelBuffers32 = inChannels_ > 0 ? inPtrs : nullptr;
        AudioBusBuffers output{};
        output.numChannels = outChannels_;
        output.channelBuffers32 = outPtrs;

        const int64 pos = samplePos_.fetch_add(frames);
        context_.sampleRate = sampleRate_;
        context_.projectTimeSamples = pos;
        context_.continousTimeSamples = pos;
        context_.projectTimeMusic = static_cast<double>(pos) / sampleRate_ * context_.tempo / 60.0;
        context_.systemTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();

        ProcessData data;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = frames;
        data.numInputs = inChannels_ > 0 ? 1 : 0;
        data.numOutputs = 1;
        data.inputs = data.numInputs ? &input : nullptr;
        data.outputs = &output;
        data.inputParameterChanges = local.empty() ? nullptr : &changes;
        data.processContext = &context_;
        processor_->process(data);

        if (outChannels_ <= 1) {
            std::copy_n(outBuffers_[0].data(), n, left);
            std::copy_n(outBuffers_[0].data(), n, right);
        } else {
            std::copy_n(outBuffers_[0].data(), n, left);
            std::copy_n(outBuffers_[1].data(), n, right);
        }
    }

    PluginInfo info_;
    VST3::Hosting::Module::Ptr module_;
    IPtr<PlugProvider> provider_;
    IPtr<BvComponentHandler> handler_;
    FUnknownPtr<IAudioProcessor> processor_;
    IPtr<IComponent> component_;
    IPtr<IEditController> controller_;
    IPtr<IPlugView> view_;
    IPtr<BvPlugFrame> frame_;
    std::vector<Param> params_;
    std::vector<std::vector<float>> inBuffers_;
    std::vector<std::vector<float>> outBuffers_;
    ProcessContext context_{};
    std::string deferred_;
    std::mutex changeMu_;
    struct Change {
        ParamID id = 0;
        float value = 0;
    };
    std::vector<Change> changes_;
    std::atomic<bool> bypassed_{false};
    std::atomic<int64_t> samplePos_{0};
    double sampleRate_ = 48000;
    int maxBlock_ = 1024;
    int inChannels_ = 0;
    int outChannels_ = 0;
    bool active_ = false;
    bool resizable_ = false;
};

VST3::Hosting::Module::Ptr openModule(const std::wstring& path, std::string& error) {
    const std::string utf8 = narrow(path);
    auto module = VST3::Hosting::Module::create(utf8, error);
    if (!module) {
        if (error.empty()) {
            error = "Could not open the VST3 module.";
        }
        return nullptr;
    }
    module->getFactory().setHostContext(hostContext());
    return module;
}

} // namespace

bool describeVst3File(const std::wstring& path, std::vector<Vst3ClassInfo>& classes, std::string& error) {
    auto module = openModule(path, error);
    if (!module) {
        return false;
    }
    for (const auto& classInfo : module->getFactory().classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass) {
            continue;
        }
        Vst3ClassInfo item;
        item.uid = classInfo.ID().toString();
        item.name = classInfo.name().empty() ? "VST3 plugin" : classInfo.name();
        item.vendor = classInfo.vendor();
        item.effect = isMusicalEffect(classInfo);
        classes.push_back(std::move(item));
    }
    return !classes.empty();
}

std::unique_ptr<Plugin> loadVst3Plugin(const PluginInfo& info, std::string& error) {
    auto module = openModule(widen(info.path), error);
    if (!module) {
        return nullptr;
    }
    const auto classes = module->getFactory().classInfos();
    const VST3::Hosting::ClassInfo* chosen = nullptr;
    for (const auto& classInfo : classes) {
        if (classInfo.category() != kVstAudioEffectClass) {
            continue;
        }
        if (!info.uid.empty() && classInfo.ID().toString() != info.uid) {
            continue;
        }
        if (info.uid.empty() && !isMusicalEffect(classInfo)) {
            continue;
        }
        chosen = &classInfo;
        break;
    }
    if (!chosen) {
        error = "No audio effect was found in that VST3 file.";
        return nullptr;
    }
    auto provider = owned(new PlugProvider(module->getFactory(), *chosen, true));
    if (!provider || !provider->initialize()) {
        error = "The plugin failed to initialize.";
        return nullptr;
    }
    PluginInfo resolved = info;
    resolved.uid = chosen->ID().toString();
    if (resolved.name.empty()) {
        resolved.name = chosen->name();
    }
    if (resolved.vendor.empty()) {
        resolved.vendor = chosen->vendor();
    }
    auto plugin = std::make_unique<Vst3Plugin>(std::move(resolved), std::move(module), provider);
    if (!plugin->info().name.empty() && plugin->parameterCount() < 0) {
        error = "The plugin could not be created.";
        return nullptr;
    }
    return plugin;
}

} // namespace bv
