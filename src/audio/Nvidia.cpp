#include "audio/Nvidia.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace bv {
namespace {

constexpr int kModel48 = 48000;
constexpr int kModel16 = 16000;

struct SdkFiles {
    std::wstring dll;
    std::wstring dllDir;
    std::wstring denoiser48;
    std::wstring denoiser16;
    std::wstring dereverb48;
    std::wstring dereverb16;
    std::wstring both48;
    std::wstring both16;
};

bool existsFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

void remember(std::wstring& slot, const std::filesystem::path& path) {
    if (slot.empty() && existsFile(path)) {
        slot = path.wstring();
    }
}

void takeFile(SdkFiles& files, const std::filesystem::path& path) {
    const std::wstring name = path.filename().wstring();
    auto same = [&](const wchar_t* expected) {
        return _wcsicmp(name.c_str(), expected) == 0;
    };
    if (same(L"NVAudioEffects.dll")) {
        if (files.dll.empty()) {
            files.dll = path.wstring();
            files.dllDir = path.parent_path().wstring();
        }
        return;
    }
    if (same(L"denoiser_48k.trtpkg")) remember(files.denoiser48, path);
    else if (same(L"denoiser_16k.trtpkg")) remember(files.denoiser16, path);
    else if (same(L"dereverb_48k.trtpkg")) remember(files.dereverb48, path);
    else if (same(L"dereverb_16k.trtpkg")) remember(files.dereverb16, path);
    else if (same(L"dereverb_denoiser_48k.trtpkg")) remember(files.both48, path);
    else if (same(L"dereverb_denoiser_16k.trtpkg")) remember(files.both16, path);
}

void walk(const std::filesystem::path& root, SdkFiles& files) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return;
    }
    std::vector<std::pair<std::filesystem::path, int>> dirs;
    dirs.emplace_back(root, 0);
    for (size_t index = 0; index < dirs.size(); ++index) {
        for (const auto& entry : std::filesystem::directory_iterator(
                 dirs[index].first, std::filesystem::directory_options::skip_permission_denied, ec)) {
            std::error_code kind;
            if (entry.is_directory(kind)) {
                if (dirs[index].second < 6) {
                    dirs.emplace_back(entry.path(), dirs[index].second + 1);
                }
            } else if (entry.is_regular_file(kind)) {
                takeFile(files, entry.path());
            }
        }
    }
}

std::wstring envVar(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    GetEnvironmentVariableW(name, value.data(), length);
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

const SdkFiles& sdkFiles() {
    static const SdkFiles files = [] {
        SdkFiles found;
        std::vector<std::wstring> roots;
        if (std::wstring dir = envVar(L"NVAFX_SDK_DIR"); !dir.empty()) {
            roots.push_back(std::move(dir));
        }
        if (std::wstring dir = envVar(L"AFX_SDK_ROOT"); !dir.empty()) {
            roots.push_back(std::move(dir));
        }
        if (std::wstring programs = envVar(L"ProgramFiles"); !programs.empty()) {
            roots.push_back((std::filesystem::path(programs) / L"NVIDIA Corporation" / L"NVIDIA Audio Effects SDK").wstring());
        }
        roots.emplace_back(L"C:\\Program Files\\NVIDIA Corporation\\NVIDIA Audio Effects SDK");
        for (const std::wstring& root : roots) {
            const std::filesystem::path base(root);
            remember(found.dll, base / L"NVAudioEffects.dll");
            if (!found.dll.empty() && found.dllDir.empty()) {
                found.dllDir = base.wstring();
            }
            remember(found.denoiser48, base / L"models" / L"denoiser_48k.trtpkg");
            remember(found.denoiser16, base / L"models" / L"denoiser_16k.trtpkg");
            remember(found.dereverb48, base / L"models" / L"dereverb_48k.trtpkg");
            remember(found.dereverb16, base / L"models" / L"dereverb_16k.trtpkg");
            remember(found.both48, base / L"models" / L"dereverb_denoiser_48k.trtpkg");
            remember(found.both16, base / L"models" / L"dereverb_denoiser_16k.trtpkg");
            if (found.dll.empty() || found.denoiser48.empty() || found.dereverb48.empty()) {
                walk(base, found);
            }
        }
        auto absolute = [](std::wstring& path) {
            if (path.empty()) {
                return;
            }
            std::error_code ec;
            const std::filesystem::path full = std::filesystem::absolute(path, ec);
            if (!ec) {
                path = full.wstring();
            }
        };
        absolute(found.dll);
        absolute(found.denoiser48);
        absolute(found.denoiser16);
        absolute(found.dereverb48);
        absolute(found.dereverb16);
        absolute(found.both48);
        absolute(found.both16);
        if (!found.dll.empty()) {
            found.dllDir = std::filesystem::path(found.dll).parent_path().wstring();
        }
        return found;
    }();
    return files;
}

std::string acp(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

const wchar_t* modelAt(const SdkFiles& files, bool noise, bool room, bool both, int rate) {
    if (both) {
        return rate == kModel16 ? files.both16.c_str() : files.both48.c_str();
    }
    if (noise) {
        return rate == kModel16 ? files.denoiser16.c_str() : files.denoiser48.c_str();
    }
    if (room) {
        return rate == kModel16 ? files.dereverb16.c_str() : files.dereverb48.c_str();
    }
    return L"";
}

bool modelExists(const SdkFiles& files, bool noise, bool room, bool both, int rate) {
    const wchar_t* path = modelAt(files, noise, room, both, rate);
    return path != nullptr && path[0] != L'\0';
}

} // namespace

struct NvidiaFilter::Shift {
    void setup(int inRate, int outRate) {
        step_ = outRate > 0 ? static_cast<double>(inRate) / static_cast<double>(outRate) : 1.0;
        pos_ = 0;
        samples_.clear();
    }

    void push(const float* samples, int count) {
        if (samples != nullptr && count > 0) {
            samples_.insert(samples_.end(), samples, samples + count);
        }
    }

    int pull(float* dest, int maxCount) {
        int wrote = 0;
        while (wrote < maxCount && pos_ + 1.0 < static_cast<double>(samples_.size())) {
            const size_t index = static_cast<size_t>(pos_);
            const double frac = pos_ - static_cast<double>(index);
            dest[wrote++] = static_cast<float>(samples_[index] * (1.0 - frac) + samples_[index + 1] * frac);
            pos_ += step_;
        }
        const size_t drop = static_cast<size_t>(pos_);
        if (drop > 0 && drop <= samples_.size()) {
            samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(drop));
            pos_ -= static_cast<double>(drop);
        }
        return wrote;
    }

    double step_ = 1;
    double pos_ = 0;
    std::vector<float> samples_;
};

struct NvidiaFilter::Api {
    using Create = int(__cdecl*)(const char*, void**);
    using Destroy = int(__cdecl*)(void*);
    using SetString = int(__cdecl*)(void*, const char*, const char*);
    using SetU32 = int(__cdecl*)(void*, const char*, unsigned int);
    using SetFloat = int(__cdecl*)(void*, const char*, float);
    using GetU32 = int(__cdecl*)(void*, const char*, unsigned int*);
    using Load = int(__cdecl*)(void*);
    using Run = int(__cdecl*)(void*, const float**, float**, unsigned int, unsigned int);
    using CuInit = int(__cdecl*)(unsigned int);
    using CuGet = int(__cdecl*)(void**);
    using CuPop = int(__cdecl*)(void**);

    HMODULE dll = nullptr;
    HMODULE cuda = nullptr;
    Create create = nullptr;
    Destroy destroy = nullptr;
    SetString setString = nullptr;
    SetU32 setU32 = nullptr;
    SetFloat setFloat = nullptr;
    GetU32 getU32 = nullptr;
    Load load = nullptr;
    Run run = nullptr;
    CuInit cuInit = nullptr;
    CuGet cuGet = nullptr;
    CuPop cuPop = nullptr;

    ~Api() {
        if (dll) {
            FreeLibrary(dll);
        }
        if (cuda) {
            FreeLibrary(cuda);
        }
    }
};

struct NvidiaFilter::Stage {
    void* effect = nullptr;
    unsigned frame = 0;
    std::vector<float> pending;
    size_t read = 0;
    std::vector<float> frameOut;

    void push(const float* samples, int count, std::vector<float>& produced, Api::Run run) {
        if (samples != nullptr && count > 0) {
            pending.insert(pending.end(), samples, samples + count);
        }
        while (frame > 0 && pending.size() - read >= frame) {
            const float* input = pending.data() + read;
            const float* inputs[] = {input};
            float* outputs[] = {frameOut.data()};
            if (run == nullptr || effect == nullptr || run(effect, inputs, outputs, frame, 1) != 0) {
                produced.insert(produced.end(), input, input + frame);
            } else {
                produced.insert(produced.end(), frameOut.begin(), frameOut.end());
            }
            read += frame;
        }
        if (read > 4096 && read * 2 > pending.size()) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(read));
            read = 0;
        }
    }
};

NvidiaSupport probeNvidia() {
    const SdkFiles& files = sdkFiles();
    NvidiaSupport support;
    support.sdk = !files.dll.empty();
    support.noise = !files.denoiser48.empty() || !files.denoiser16.empty() || !files.both48.empty() || !files.both16.empty();
    support.room = !files.dereverb48.empty() || !files.dereverb16.empty() || !files.both48.empty() || !files.both16.empty();
    return support;
}

NvidiaFilter::~NvidiaFilter() {
    close();
    delete toModel_;
    delete toDevice_;
    delete api_;
}

bool NvidiaFilter::loadApi(std::string& error) {
    if (api_ != nullptr) {
        return true;
    }
    const SdkFiles& files = sdkFiles();
    if (files.dll.empty()) {
        error = "The NVIDIA Audio Effects SDK is not installed.";
        return false;
    }
    auto* api = new Api();
    SetDllDirectoryW(files.dllDir.c_str());
    api->dll = LoadLibraryExW(files.dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    SetDllDirectoryW(nullptr);
    if (api->dll == nullptr) {
        delete api;
        error = "The NVIDIA Audio Effects SDK could not be loaded.";
        return false;
    }
    api->create = reinterpret_cast<Api::Create>(GetProcAddress(api->dll, "NvAFX_CreateEffect"));
    api->destroy = reinterpret_cast<Api::Destroy>(GetProcAddress(api->dll, "NvAFX_DestroyEffect"));
    api->setString = reinterpret_cast<Api::SetString>(GetProcAddress(api->dll, "NvAFX_SetString"));
    api->setU32 = reinterpret_cast<Api::SetU32>(GetProcAddress(api->dll, "NvAFX_SetU32"));
    api->setFloat = reinterpret_cast<Api::SetFloat>(GetProcAddress(api->dll, "NvAFX_SetFloat"));
    api->getU32 = reinterpret_cast<Api::GetU32>(GetProcAddress(api->dll, "NvAFX_GetU32"));
    api->load = reinterpret_cast<Api::Load>(GetProcAddress(api->dll, "NvAFX_Load"));
    api->run = reinterpret_cast<Api::Run>(GetProcAddress(api->dll, "NvAFX_Run"));
    if (api->create == nullptr || api->destroy == nullptr || api->setString == nullptr || api->setU32 == nullptr ||
        api->getU32 == nullptr || api->load == nullptr || api->run == nullptr) {
        delete api;
        error = "This NVIDIA Audio Effects SDK is missing the expected functions.";
        return false;
    }
    api->cuda = LoadLibraryW(L"nvcuda.dll");
    if (api->cuda != nullptr) {
        api->cuInit = reinterpret_cast<Api::CuInit>(GetProcAddress(api->cuda, "cuInit"));
        api->cuGet = reinterpret_cast<Api::CuGet>(GetProcAddress(api->cuda, "cuCtxGetCurrent"));
        api->cuPop = reinterpret_cast<Api::CuPop>(GetProcAddress(api->cuda, "cuCtxPopCurrent"));
        if (api->cuInit != nullptr) {
            api->cuInit(0);
        }
    }
    api_ = api;
    return true;
}

bool NvidiaFilter::openStage(Stage& stage, const char* effect, const std::wstring& model, int modelRate, std::string& error) {
    const std::string path = acp(model);
    if (path.empty()) {
        error = "The NVIDIA model path could not be used.";
        return false;
    }
    void* oldContext = nullptr;
    const bool hadContext = api_->cuGet != nullptr && api_->cuGet(&oldContext) == 0;
    void* handle = nullptr;
    if (api_->create(effect, &handle) != 0 || handle == nullptr) {
        error = "Could not create the NVIDIA audio effect.";
        return false;
    }
    void* current = nullptr;
    if (hadContext && api_->cuGet(&current) == 0 && current != oldContext && api_->cuPop != nullptr) {
        api_->cuPop(nullptr);
    }
    auto fail = [&](const char* message) {
        api_->destroy(handle);
        error = message;
        return false;
    };
    if (api_->setU32(handle, "input_sample_rate", static_cast<unsigned>(modelRate)) != 0 &&
        api_->setU32(handle, "sample_rate", static_cast<unsigned>(modelRate)) != 0) {
        return fail("The NVIDIA audio effect rejected the sample rate.");
    }
    api_->setU32(handle, "output_sample_rate", static_cast<unsigned>(modelRate));
    if (api_->setFloat != nullptr) {
        api_->setFloat(handle, "intensity_ratio", intensityPercent_.load(std::memory_order_relaxed) / 100.f);
    }
    api_->setU32(handle, "enable_vad", 1);
    if (api_->setString(handle, "model_path", path.c_str()) != 0) {
        return fail("The NVIDIA model could not be selected.");
    }
    // Load pulls the model onto the GPU. It can take a few seconds and must stay off the audio thread.
    if (api_->load(handle) != 0) {
        return fail("Could not load the NVIDIA audio model. The GPU may be unsupported.");
    }
    unsigned channels = 1;
    if (api_->getU32(handle, "num_input_channels", &channels) != 0) {
        api_->getU32(handle, "num_channels", &channels);
    }
    if (channels != 1) {
        return fail("The NVIDIA audio effect is not mono.");
    }
    unsigned frame = 0;
    if (api_->getU32(handle, "num_input_samples_per_frame", &frame) != 0) {
        api_->getU32(handle, "num_samples_per_frame", &frame);
    }
    unsigned outputFrame = frame;
    if (api_->getU32(handle, "num_output_samples_per_frame", &outputFrame) == 0 && outputFrame != frame) {
        return fail("The NVIDIA audio effect changes the frame length.");
    }
    if (frame == 0 || frame > 16384) {
        return fail("The NVIDIA audio effect reported an unsupported frame size.");
    }
    stage.effect = handle;
    stage.frame = frame;
    stage.frameOut.assign(frame, 0.f);
    stage.pending.clear();
    stage.read = 0;
    return true;
}

void NvidiaFilter::destroyStage(Stage& stage) {
    if (stage.effect != nullptr && api_ != nullptr && api_->destroy != nullptr) {
        api_->destroy(stage.effect);
    }
    stage = {};
}

bool NvidiaFilter::open(int deviceRate, bool noise, bool room, std::string& error) {
    close();
    error.clear();
    if (!noise && !room) {
        return true;
    }
    if (deviceRate <= 0) {
        error = "The audio device did not report a sample rate.";
        return false;
    }
    if (!loadApi(error)) {
        return false;
    }
    const SdkFiles& files = sdkFiles();
    const bool prefer16 = deviceRate <= 24000;
    int modelRate = 0;
    const int order[2] = {prefer16 ? kModel16 : kModel48, prefer16 ? kModel48 : kModel16};
    for (int candidate : order) {
        const bool noiseOk = !noise || modelExists(files, true, false, false, candidate) || modelExists(files, false, false, true, candidate);
        const bool roomOk = !room || modelExists(files, false, true, false, candidate) || modelExists(files, false, false, true, candidate);
        if (noiseOk && roomOk && (noise || room)) {
            modelRate = candidate;
            break;
        }
    }
    if (modelRate == 0) {
        error = "The NVIDIA noise or room model was not found.";
        return false;
    }

    auto pathFor = [&](bool wantNoise, bool wantRoom, bool wantBoth) {
        return std::wstring(modelAt(files, wantNoise, wantRoom, wantBoth, modelRate));
    };
    SetDllDirectoryW(files.dllDir.c_str());
    bool opened = false;
    if (noise && room && modelExists(files, false, false, true, modelRate)) {
        room_ = new Stage();
        opened = openStage(*room_, "dereverb_denoiser", pathFor(false, false, true), modelRate, error);
        if (!opened) {
            destroyStage(*room_);
            delete room_;
            room_ = nullptr;
        }
    }
    if (!opened) {
        error.clear();
        if (room) {
            const std::wstring path = modelExists(files, false, true, false, modelRate) ? pathFor(false, true, false) : pathFor(false, false, true);
            const char* effect = modelExists(files, false, true, false, modelRate) ? "dereverb" : "dereverb_denoiser";
            room_ = new Stage();
            if (!openStage(*room_, effect, path, modelRate, error)) {
                destroyStage(*room_);
                delete room_;
                room_ = nullptr;
                SetDllDirectoryW(nullptr);
                return false;
            }
        }
        if (noise && !(room && room_ != nullptr && !modelExists(files, false, true, false, modelRate) &&
                       modelExists(files, false, false, true, modelRate))) {
            const std::wstring path = modelExists(files, true, false, false, modelRate) ? pathFor(true, false, false) : pathFor(false, false, true);
            const char* effect = modelExists(files, true, false, false, modelRate) ? "denoiser" : "dereverb_denoiser";
            noise_ = new Stage();
            if (!openStage(*noise_, effect, path, modelRate, error)) {
                destroyStage(*noise_);
                delete noise_;
                noise_ = nullptr;
                close();
                SetDllDirectoryW(nullptr);
                return false;
            }
        }
    }
    SetDllDirectoryW(nullptr);

    deviceRate_ = deviceRate;
    modelRate_ = modelRate;
    if (deviceRate_ != modelRate_) {
        toModel_ = new Shift();
        toDevice_ = new Shift();
        toModel_->setup(deviceRate_, modelRate_);
        toDevice_->setup(modelRate_, deviceRate_);
    }
    unsigned delay = 0;
    if (room_ != nullptr) {
        delay += room_->frame;
    }
    if (noise_ != nullptr) {
        delay += noise_->frame;
    }
    if (delay == 0) {
        delay = 480;
    }
    // One model frame of silence keeps the output aligned while a partial frame is still waiting.
    const int deviceDelay = deviceRate_ == modelRate_
                                ? static_cast<int>(delay)
                                : static_cast<int>(std::ceil(static_cast<double>(delay) * deviceRate_ / modelRate_)) + 8;
    out_.assign(static_cast<size_t>(std::max(deviceDelay, 1)), 0.f);
    outRead_ = 0;
    appliedPercent_ = intensityPercent_.load(std::memory_order_relaxed);
    active_ = true;
    return true;
}

void NvidiaFilter::setIntensity(float ratio) {
    const int percent = std::clamp(static_cast<int>(std::lround(ratio * 100.f)), 0, 100);
    intensityPercent_.store(percent, std::memory_order_relaxed);
}

void NvidiaFilter::applyIntensity() {
    const int percent = intensityPercent_.load(std::memory_order_relaxed);
    if (percent == appliedPercent_ || api_ == nullptr || api_->setFloat == nullptr) {
        return;
    }
    const float ratio = percent / 100.f;
    if (room_ != nullptr && room_->effect != nullptr) {
        api_->setFloat(room_->effect, "intensity_ratio", ratio);
    }
    if (noise_ != nullptr && noise_->effect != nullptr) {
        api_->setFloat(noise_->effect, "intensity_ratio", ratio);
    }
    appliedPercent_ = percent;
}

void NvidiaFilter::close() {
    active_ = false;
    if (room_ != nullptr) {
        destroyStage(*room_);
        delete room_;
        room_ = nullptr;
    }
    if (noise_ != nullptr) {
        destroyStage(*noise_);
        delete noise_;
        noise_ = nullptr;
    }
    delete toModel_;
    delete toDevice_;
    toModel_ = nullptr;
    toDevice_ = nullptr;
    out_.clear();
    outRead_ = 0;
    deviceRate_ = 0;
    modelRate_ = 0;
    appliedPercent_ = -1;
}

void NvidiaFilter::pushChain(const float* samples, int count) {
    std::vector<float> current(samples, samples + count);
    std::vector<float> next;
    Stage* stages[2] = {room_, noise_};
    bool any = false;
    for (Stage* stage : stages) {
        if (stage == nullptr) {
            continue;
        }
        any = true;
        next.clear();
        stage->push(current.data(), static_cast<int>(current.size()), next, api_->run);
        current.swap(next);
    }
    if (!any) {
        return;
    }
    if (toDevice_ == nullptr) {
        out_.insert(out_.end(), current.begin(), current.end());
        return;
    }
    toDevice_->push(current.data(), static_cast<int>(current.size()));
    std::vector<float> back(static_cast<size_t>(count) + current.size() + 8);
    const int wrote = toDevice_->pull(back.data(), static_cast<int>(back.size()));
    if (wrote > 0) {
        out_.insert(out_.end(), back.begin(), back.begin() + wrote);
    }
}

void NvidiaFilter::process(float* left, float* right, int frames) {
    if (!active_ || frames <= 0 || left == nullptr || right == nullptr) {
        return;
    }
    applyIntensity();
    std::vector<float> mono(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        mono[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
    }
    if (toModel_ == nullptr) {
        pushChain(mono.data(), frames);
    } else {
        toModel_->push(mono.data(), frames);
        std::vector<float> model(static_cast<size_t>(frames) * static_cast<size_t>(modelRate_) / static_cast<size_t>(std::max(deviceRate_, 1)) + 16);
        const int count = toModel_->pull(model.data(), static_cast<int>(model.size()));
        if (count > 0) {
            pushChain(model.data(), count);
        }
    }
    for (int i = 0; i < frames; ++i) {
        const float sample = outRead_ < out_.size() ? out_[outRead_++] : 0.f;
        left[i] = sample;
        right[i] = sample;
    }
    if (outRead_ > 8192 && outRead_ * 2 > out_.size()) {
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(outRead_));
        outRead_ = 0;
    }
}

} // namespace bv
