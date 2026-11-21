#include "audio/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <xmmintrin.h>

namespace bv {
namespace {

using Microsoft::WRL::ComPtr;

constexpr size_t kQueueFrames = 65536;

void ensureCom() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

std::wstring lower(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return text;
}

bool looksVirtual(const std::wstring& name) {
    const std::wstring n = lower(name);
    static constexpr const wchar_t* kNeedles[] = {
        L"cable",    L"loopback", L"voicemeeter", L"vb-audio", L"vb audio",
        L"virtual",  L"blackhole", L"synchronous audio", L"wave link", L"elgato",
    };
    for (const wchar_t* needle : kNeedles) {
        if (n.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::wstring deviceName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
        return L"Unknown device";
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name = L"Unknown device";
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

std::wstring defaultId(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device))) {
        return {};
    }
    LPWSTR raw = nullptr;
    if (FAILED(device->GetId(&raw)) || !raw) {
        return {};
    }
    std::wstring id = raw;
    CoTaskMemFree(raw);
    return id;
}

std::string hrText(HRESULT hr) {
    switch (hr) {
    case AUDCLNT_E_DEVICE_IN_USE:
        return "The device is already in exclusive use.";
    case AUDCLNT_E_DEVICE_INVALIDATED:
        return "The audio device was disconnected.";
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return "This device format is not supported.";
    case E_ACCESSDENIED:
        return "Microphone access was denied in Windows privacy settings.";
    default:
        break;
    }
    char buffer[16];
    sprintf_s(buffer, "%08X", static_cast<unsigned>(hr));
    return std::string("Audio device failed (0x") + buffer + ").";
}

struct PcmFormat {
    int channels = 0;
    int sampleRate = 0;
    int bytesPerSample = 0;
    bool isFloat = false;
    int validBits = 0;
};

bool parseFormat(const WAVEFORMATEX* format, PcmFormat& out) {
    if (!format || format->nChannels <= 0 || format->nSamplesPerSec == 0) {
        return false;
    }
    out.channels = format->nChannels;
    out.sampleRate = static_cast<int>(format->nSamplesPerSec);
    const WAVEFORMATEXTENSIBLE* ext = nullptr;
    bool pcm = false;
    bool flt = false;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            flt = true;
        } else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            pcm = true;
        } else {
            return false;
        }
        out.validBits = ext->Samples.wValidBitsPerSample;
    } else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        flt = true;
        out.validBits = format->wBitsPerSample;
    } else if (format->wFormatTag == WAVE_FORMAT_PCM) {
        pcm = true;
        out.validBits = format->wBitsPerSample;
    } else {
        return false;
    }
    out.isFloat = flt;
    out.bytesPerSample = format->nBlockAlign / format->nChannels;
    if (out.bytesPerSample <= 0) {
        return false;
    }
    if (pcm && out.bytesPerSample != 2 && out.bytesPerSample != 3 && out.bytesPerSample != 4) {
        return false;
    }
    if (flt && out.bytesPerSample != 4) {
        return false;
    }
    return true;
}

float readSample(const BYTE* p, const PcmFormat& fmt) {
    if (fmt.isFloat) {
        float value = 0.f;
        std::memcpy(&value, p, sizeof(float));
        return value;
    }
    if (fmt.bytesPerSample == 2) {
        int16_t value = 0;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(value) / 32768.f;
    }
    if (fmt.bytesPerSample == 3) {
        int32_t value = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
        return static_cast<float>(value) / 2147483648.f;
    }
    int32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    if (fmt.validBits > 0 && fmt.validBits < 32) {
        value >>= (32 - fmt.validBits);
        const float scale = static_cast<float>(1u << (fmt.validBits - 1));
        return static_cast<float>(value) / scale;
    }
    return static_cast<float>(value) / 2147483648.f;
}

void writeFloat(BYTE* dst, float left, float right, const PcmFormat& fmt) {
    auto store = [&](BYTE* p, float sample) {
        sample = std::clamp(sample, -1.f, 1.f);
        if (fmt.isFloat) {
            std::memcpy(p, &sample, sizeof(float));
            return;
        }
        if (fmt.bytesPerSample == 2) {
            const auto value = static_cast<int16_t>(std::lrintf(sample * 32767.f));
            std::memcpy(p, &value, sizeof(value));
            return;
        }
        if (fmt.bytesPerSample == 3) {
            const auto value = static_cast<int32_t>(std::lrintf(sample * 8388607.f));
            p[0] = static_cast<BYTE>(value & 0xFF);
            p[1] = static_cast<BYTE>((value >> 8) & 0xFF);
            p[2] = static_cast<BYTE>((value >> 16) & 0xFF);
            return;
        }
        const auto value = static_cast<int32_t>(std::lrintf(sample * 2147483647.f));
        std::memcpy(p, &value, sizeof(value));
    };
    if (fmt.channels == 1) {
        store(dst, 0.5f * (left + right));
        return;
    }
    store(dst, left);
    store(dst + fmt.bytesPerSample, right);
    const int stride = fmt.bytesPerSample;
    for (int ch = 2; ch < fmt.channels; ++ch) {
        std::memset(dst + ch * stride, 0, stride);
    }
}

void toStereo(const BYTE* src, uint32_t frames, const PcmFormat& fmt, float* interleaved) {
    const int stride = fmt.bytesPerSample * fmt.channels;
    for (uint32_t i = 0; i < frames; ++i) {
        const BYTE* frame = src + static_cast<size_t>(i) * stride;
        float left = readSample(frame, fmt);
        float right = left;
        if (fmt.channels > 1) {
            right = readSample(frame + fmt.bytesPerSample, fmt);
        }
        interleaved[i * 2] = left;
        interleaved[i * 2 + 1] = right;
    }
}

class StereoQueue {
public:
    StereoQueue() : data_(kQueueFrames * 2) {}

    size_t available() const {
        std::lock_guard lock(mu_);
        return (write_ + kQueueFrames - read_) % kQueueFrames;
    }

    void push(const float* interleaved, size_t frames) {
        std::lock_guard lock(mu_);
        for (size_t i = 0; i < frames; ++i) {
            const size_t next = (write_ + 1) % kQueueFrames;
            if (next == read_) {
                read_ = (read_ + 1) % kQueueFrames;
            }
            data_[write_ * 2] = interleaved[i * 2];
            data_[write_ * 2 + 1] = interleaved[i * 2 + 1];
            write_ = next;
        }
    }

    size_t pull(float* interleaved, size_t frames) {
        std::lock_guard lock(mu_);
        size_t have = (write_ + kQueueFrames - read_) % kQueueFrames;
        const size_t n = std::min(have, frames);
        for (size_t i = 0; i < n; ++i) {
            interleaved[i * 2] = data_[read_ * 2];
            interleaved[i * 2 + 1] = data_[read_ * 2 + 1];
            read_ = (read_ + 1) % kQueueFrames;
        }
        return n;
    }

    void discardDownTo(size_t frames) {
        std::lock_guard lock(mu_);
        size_t have = (write_ + kQueueFrames - read_) % kQueueFrames;
        if (have > frames) {
            read_ = (write_ + kQueueFrames - frames) % kQueueFrames;
        }
    }

    void clear() {
        std::lock_guard lock(mu_);
        read_ = write_ = 0;
    }

private:
    mutable std::mutex mu_;
    std::vector<float> data_;
    size_t read_ = 0;
    size_t write_ = 0;
};

class LinearResampler {
public:
    void setup(double inRate, double outRate) {
        active_ = std::abs(inRate - outRate) > 0.5;
        step_ = outRate > 0 ? inRate / outRate : 1.0;
        pos_ = 0;
        pending_.clear();
    }

    void process(const float* in, size_t inFrames, std::vector<float>& out) {
        if (!active_) {
            out.insert(out.end(), in, in + inFrames * 2);
            return;
        }
        pending_.insert(pending_.end(), in, in + inFrames * 2);
        const size_t frames = pending_.size() / 2;
        while (pos_ + 1.0 < static_cast<double>(frames)) {
            const size_t i = static_cast<size_t>(pos_);
            const double frac = pos_ - static_cast<double>(i);
            const float l = static_cast<float>(pending_[i * 2] * (1.0 - frac) +
                                               pending_[(i + 1) * 2] * frac);
            const float r = static_cast<float>(pending_[i * 2 + 1] * (1.0 - frac) +
                                               pending_[(i + 1) * 2 + 1] * frac);
            out.push_back(l);
            out.push_back(r);
            pos_ += step_;
        }
        const size_t drop = static_cast<size_t>(pos_);
        if (drop > 0 && drop <= frames) {
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(drop * 2));
            pos_ -= static_cast<double>(drop);
        }
    }

private:
    bool active_ = false;
    double step_ = 1;
    double pos_ = 0;
    std::vector<float> pending_;
};

struct OpenedClient {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    WAVEFORMATEX* format = nullptr;
    PcmFormat pcm;
    UINT32 bufferFrames = 0;

    ~OpenedClient() {
        if (format) {
            CoTaskMemFree(format);
        }
    }

    OpenedClient() = default;
    OpenedClient(const OpenedClient&) = delete;
    OpenedClient& operator=(const OpenedClient&) = delete;
};

bool openDevice(const std::wstring& id, bool capture, uint32_t requestedFrames, HANDLE event,
                OpenedClient& out, std::string& error) {
    ensureCom();
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) {
        error = "That audio device is no longer available.";
        return false;
    }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(out.client.GetAddressOf()));
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    hr = out.client->GetMixFormat(&out.format);
    if (FAILED(hr) || !parseFormat(out.format, out.pcm)) {
        error = "The device mix format cannot be used.";
        return false;
    }

    // Shared-mode render or capture. The output side writes into the device the
    // user picked. For VB-Cable that device is the playback endpoint "CABLE Input";
    // other apps then record "CABLE Output". This is not loopback capture of the
    // system mix, and it is not the VST3 SDK AudioHost sample (that sample is Jack).
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
    ComPtr<IAudioClient3> client3;
    hr = E_FAIL;
    if (SUCCEEDED(out.client.As(&client3))) {
        UINT32 defPeriod = 0, fundamental = 0, minPeriod = 0, maxPeriod = 0;
        if (SUCCEEDED(client3->GetSharedModeEnginePeriod(out.format, &defPeriod, &fundamental,
                                                         &minPeriod, &maxPeriod)) &&
            fundamental > 0) {
            UINT32 period = requestedFrames;
            if (period < minPeriod) period = minPeriod;
            if (period > maxPeriod) period = maxPeriod;
            period = std::max(minPeriod, (period / fundamental) * fundamental);
            hr = client3->InitializeSharedAudioStream(flags, period, out.format, nullptr);
        }
    }
    if (FAILED(hr)) {
        const REFERENCE_TIME duration = static_cast<REFERENCE_TIME>(
            10000000.0 * static_cast<double>(requestedFrames) / out.pcm.sampleRate);
        hr = out.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, out.format, nullptr);
    }
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    hr = out.client->GetBufferSize(&out.bufferFrames);
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    hr = out.client->SetEventHandle(event);
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    if (capture) {
        hr = out.client->GetService(IID_PPV_ARGS(&out.capture));
    } else {
        hr = out.client->GetService(IID_PPV_ARGS(&out.render));
    }
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    hr = out.client->Start();
    if (FAILED(hr)) {
        error = hrText(hr);
        return false;
    }
    return true;
}

void raiseThreadPriority() {
    DWORD task = 0;
    if (HANDLE handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task)) {
        AvSetMmThreadPriority(handle, AVRT_PRIORITY_HIGH);
    }
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

struct BlockLevel {
    float peak = 0.f;
    double sumSq = 0.0;
};

// Peak is the louder channel. RMS follows that same channel, so a silent side does not pull the level down.
void noteSample(BlockLevel& level, float left, float right) {
    level.peak = std::max(level.peak, std::max(std::abs(left), std::abs(right)));
    const double leftSq = static_cast<double>(left) * left;
    const double rightSq = static_cast<double>(right) * right;
    level.sumSq += leftSq > rightSq ? leftSq : rightSq;
}

} // namespace

void ProcessChain::setPlugins(std::vector<Plugin*> plugins) {
    plugins_ = std::move(plugins);
}

void ProcessChain::process(float** stereo, int frames) {
    for (Plugin* plugin : plugins_) {
        if (plugin && !plugin->bypassed()) {
            plugin->process(stereo, frames);
        }
    }
}

struct Engine::Queues {
    StereoQueue queue;
    LinearResampler resampler;
    std::atomic<bool> primed{false};
    int outputRate = 48000;
};

std::vector<DeviceInfo> enumerateDevices() {
    ensureCom();
    std::vector<DeviceInfo> devices;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator)))) {
    const std::wstring defaultIn = defaultId(enumerator.Get(), eCapture);
    const std::wstring defaultOut = defaultId(enumerator.Get(), eRender);
    auto collect = [&](EDataFlow flow, bool capture) {
        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) {
            return;
        }
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(i, &device))) {
                continue;
            }
            LPWSTR raw = nullptr;
            if (FAILED(device->GetId(&raw)) || !raw) {
                continue;
            }
            DeviceInfo info;
            info.id = raw;
            CoTaskMemFree(raw);
            info.name = deviceName(device.Get());
            info.capture = capture;
            info.virtualDevice = looksVirtual(info.name);
            info.isDefault = info.id == (capture ? defaultIn : defaultOut);
            devices.push_back(std::move(info));
        }
    };
    collect(eCapture, true);
    collect(eRender, false);
    }
    DeviceInfo file;
    file.id = kFileOutputId;
    file.name = L"File";
    file.fileOutput = true;
    devices.push_back(std::move(file));
    std::stable_sort(devices.begin(), devices.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        if (a.capture != b.capture) {
            return a.capture && !b.capture;
        }
        if (!a.capture && a.fileOutput != b.fileOutput) {
            return a.fileOutput && !b.fileOutput;
        }
        if (!a.capture && a.virtualDevice != b.virtualDevice) {
            return a.virtualDevice && !b.virtualDevice;
        }
        return a.name < b.name;
    });
    return devices;
}

Engine::Engine() : queues_(std::make_unique<Queues>()) {}

Engine::~Engine() {
    stop();
}

bool Engine::start(const EngineConfig& config, const std::shared_ptr<ProcessChain>& chain,
                   std::string& error) {
    stop();
    levels_.resetBin();
    fileOutput_ = config.outputId == kFileOutputId;
    if (config.inputId.empty() || config.outputId.empty()) {
        error = "Choose an input and an output.";
        return false;
    }
    if (fileOutput_ && config.outputPath.empty()) {
        error = "Choose a WAV file.";
        return false;
    }
    chain_ = chain ? chain : std::make_shared<ProcessChain>();
    queues_->queue.clear();
    queues_->primed = false;
    faulted_ = false;
    {
        std::lock_guard lock(faultMu_);
        fault_.clear();
    }
    opened_ = 0;
    failed_ = 0;
    openError_.clear();
    suspendRequest_.store(0, std::memory_order_relaxed);
    audioAck_.store(0, std::memory_order_relaxed);
    hold_ = true;
    sampleRate_ = 0;
    bufferFrames_ = 0;

    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    renderEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent_ || !renderEvent_) {
        error = "Could not create audio events.";
        stop();
        return false;
    }

    const uint32_t requested = config.bufferFrames;
    captureThread_ = std::jthread([this, id = config.inputId](std::stop_token stop) {
        captureLoop(stop, std::move(id));
    });
    renderThread_ = std::jthread([this, id = config.outputId, path = config.outputPath, requested](std::stop_token stop) {
        if (fileOutput_) {
            fileLoop(stop, std::move(path), requested);
        } else {
            renderLoop(stop, std::move(id), requested);
        }
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (opened_.load() + failed_.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (failed_.load() > 0 || opened_.load() < 2) {
        {
            std::lock_guard lock(errorMu_);
            error = openError_.empty() ? "The audio devices did not start." : openError_;
        }
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void Engine::stop() {
    running_ = false;
    suspendRequest_.store(0, std::memory_order_release);
    if (captureThread_.joinable()) {
        captureThread_.request_stop();
    }
    if (renderThread_.joinable()) {
        renderThread_.request_stop();
    }
    if (captureEvent_) {
        SetEvent(static_cast<HANDLE>(captureEvent_));
    }
    if (renderEvent_) {
        SetEvent(static_cast<HANDLE>(renderEvent_));
    }
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
    if (captureEvent_) {
        CloseHandle(static_cast<HANDLE>(captureEvent_));
        captureEvent_ = nullptr;
    }
    if (renderEvent_) {
        CloseHandle(static_cast<HANDLE>(renderEvent_));
        renderEvent_ = nullptr;
    }
    nvidia_.close();
    audioAck_.store(0, std::memory_order_relaxed);
}

void Engine::suspend() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    uint32_t request = suspendRequest_.load(std::memory_order_seq_cst);
    while ((request & 1u) == 0) {
        if (suspendRequest_.compare_exchange_weak(request, request + 1, std::memory_order_seq_cst)) {
            ++request;
            break;
        }
    }
    // NvAFX_Run and a plugin buffer can outlast a short poll. Wait until this request
    // has been acknowledged from outside the effect and the chain, instead of tearing
    // them down while that work is still on the stack.
    while (audioAck_.load(std::memory_order_seq_cst) != request) {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Engine::resume() {
    uint32_t request = suspendRequest_.load(std::memory_order_seq_cst);
    while ((request & 1u) != 0) {
        if (suspendRequest_.compare_exchange_weak(request, request + 1, std::memory_order_seq_cst)) {
            break;
        }
    }
}

void Engine::setHold(bool hold) {
    hold_.store(hold, std::memory_order_release);
}

void Engine::setOutputGainDb(float db) {
    gain_.store(std::pow(10.f, db / 20.f), std::memory_order_relaxed);
}

void Engine::setInputMono(bool mono) {
    inputMono_.store(mono, std::memory_order_relaxed);
}

void Engine::setNvidia(bool noise, bool room, float intensity, std::string& error) {
    const bool live = running_.load(std::memory_order_acquire);
    const bool held = hold_.load(std::memory_order_acquire);
    if (live && !held) {
        suspend();
    }
    nvidia_.setIntensity(intensity);
    nvidiaNoise_.store(noise, std::memory_order_relaxed);
    nvidiaRoom_.store(room, std::memory_order_relaxed);
    nvidia_.close();
    error.clear();
    const int rate = static_cast<int>(sampleRate_.load(std::memory_order_acquire));
    if ((noise || room) && rate > 0 && !nvidia_.open(rate, noise, room, error)) {
        nvidia_.close();
    }
    if (live && !held) {
        resume();
    }
}

void Engine::setNvidiaIntensity(float intensity) {
    nvidia_.setIntensity(intensity);
}

void Engine::filterInput(float* left, float* right, int frames) {
    // The NVIDIA effect is the mono mix in front of the VST chain.
    if (!nvidia_.active()) {
        return;
    }
    const int status = nvidia_.process(left, right, frames);
    if (status == 0) {
        return;
    }
    std::lock_guard lock(faultMu_);
    if (fault_.empty()) {
        fault_ = status > 0
                     ? "The NVIDIA audio effect failed (NvAFX_Run error " + std::to_string(status) + ")."
                     : "The NVIDIA audio effect failed.";
    }
    faulted_.store(true, std::memory_order_release);
}

bool Engine::acknowledgeSuspend() {
    const uint32_t request = suspendRequest_.load(std::memory_order_seq_cst);
    if ((request & 1u) == 0) {
        return false;
    }
    audioAck_.store(request, std::memory_order_seq_cst);
    return true;
}

float Engine::takeInputPeak() {
    return inPeak_.exchange(0.f, std::memory_order_relaxed);
}

float Engine::takeOutputPeak() {
    return outPeak_.exchange(0.f, std::memory_order_relaxed);
}

int Engine::drainLevels(LevelSample* out, int count) {
    return levels_.drain(out, count);
}

void Engine::discardLevels() {
    levels_.discard();
}

EngineStatus Engine::status() const {
    EngineStatus status;
    status.sampleRate = sampleRate_.load();
    status.bufferFrames = bufferFrames_.load();
    if (status.sampleRate > 0) {
        const double frames = static_cast<double>(status.bufferFrames) * 3.0;
        status.latencyMs = frames / static_cast<double>(status.sampleRate) * 1000.0;
    }
    return status;
}

std::string Engine::takeFault() {
    if (!faulted_.load(std::memory_order_acquire)) {
        return {};
    }
    std::lock_guard lock(faultMu_);
    return fault_;
}

void Engine::bumpPeak(std::atomic<float>& slot, float peak) {
    float current = slot.load(std::memory_order_relaxed);
    while (peak > current && !slot.compare_exchange_weak(current, peak, std::memory_order_relaxed)) {
    }
}

namespace {

// 0 maps to silence (-140 dB), so an empty slot is not a full-scale sample.
uint16_t encodeDb(float db) {
    const float clamped = std::clamp(db, -140.f, 40.f);
    return static_cast<uint16_t>(std::lround((clamped + 140.f) * 10.f));
}

float decodeDb(uint16_t quantized) {
    return static_cast<float>(quantized) / 10.f - 140.f;
}

uint64_t encodeFrame(float inPeakDb, float inRmsDb, float outPeakDb, float outRmsDb) {
    uint64_t bits = encodeDb(inPeakDb);
    bits |= static_cast<uint64_t>(encodeDb(inRmsDb)) << 16;
    bits |= static_cast<uint64_t>(encodeDb(outPeakDb)) << 32;
    bits |= static_cast<uint64_t>(encodeDb(outRmsDb)) << 48;
    return bits;
}

LevelSample decodeFrame(uint64_t bits) {
    LevelSample sample;
    sample.inPeakDb = decodeDb(static_cast<uint16_t>(bits));
    sample.inRmsDb = decodeDb(static_cast<uint16_t>(bits >> 16));
    sample.outPeakDb = decodeDb(static_cast<uint16_t>(bits >> 32));
    sample.outRmsDb = decodeDb(static_cast<uint16_t>(bits >> 48));
    return sample;
}

float linearToDb(float linear) {
    if (!(linear > 1.e-7f)) {
        return -140.f;
    }
    return 20.f * std::log10(linear);
}

} // namespace

LevelTap::LevelTap() {
    for (auto& slot : slots_) {
        slot.store(0, std::memory_order_relaxed);
    }
}

void LevelTap::add(int sampleRate, uint32_t frames, float inPeak, double inSumSq, float outPeak, double outSumSq) {
    if (sampleRate <= 0 || frames == 0) {
        return;
    }
    if (sampleRate != rate_) {
        rate_ = sampleRate;
        window_ = std::max(1, sampleRate / kLevelBinsPerSecond);
        inPeak_ = 0.f;
        outPeak_ = 0.f;
        inSumSq_ = 0.0;
        outSumSq_ = 0.0;
        filled_ = 0;
    }
    inPeak_ = std::max(inPeak_, inPeak);
    outPeak_ = std::max(outPeak_, outPeak);
    inSumSq_ += inSumSq;
    outSumSq_ += outSumSq;
    filled_ += frames;
    // The block that crosses the window is a few milliseconds long, so the bin
    // lands between about 50 ms and 50 ms plus one buffer. That stays inside 15-25 Hz.
    if (filled_ < static_cast<uint32_t>(window_)) {
        return;
    }
    const double framesInBin = static_cast<double>(filled_);
    const float inRms = inSumSq_ > 0.0 ? static_cast<float>(std::sqrt(inSumSq_ / framesInBin)) : 0.f;
    const float outRms = outSumSq_ > 0.0 ? static_cast<float>(std::sqrt(outSumSq_ / framesInBin)) : 0.f;
    publish(linearToDb(inPeak_), linearToDb(inRms), linearToDb(outPeak_), linearToDb(outRms));
    inPeak_ = 0.f;
    outPeak_ = 0.f;
    inSumSq_ = 0.0;
    outSumSq_ = 0.0;
    filled_ = 0;
}

void LevelTap::resetBin() {
    rate_ = 0;
    window_ = 1;
    filled_ = 0;
    inPeak_ = 0.f;
    outPeak_ = 0.f;
    inSumSq_ = 0.0;
    outSumSq_ = 0.0;
}

void LevelTap::publish(float inPeakDb, float inRmsDb, float outPeakDb, float outRmsDb) {
    const uint32_t index = write_.load(std::memory_order_relaxed);
    slots_[index & (kCapacity - 1)].store(encodeFrame(inPeakDb, inRmsDb, outPeakDb, outRmsDb),
                                          std::memory_order_relaxed);
    write_.store(index + 1, std::memory_order_release);
}

int LevelTap::drain(LevelSample* out, int count) {
    if (!out || count <= 0) {
        return 0;
    }
    const uint32_t write = write_.load(std::memory_order_acquire);
    uint32_t read = read_;
    if (write - read > kCapacity - 1) {
        read = write - (kCapacity - 1);
    }
    int copied = 0;
    while (read != write && copied < count) {
        const uint64_t bits = slots_[read & (kCapacity - 1)].load(std::memory_order_relaxed);
        out[copied++] = decodeFrame(bits);
        ++read;
    }
    read_ = read;
    return copied;
}

void LevelTap::discard() {
    read_ = write_.load(std::memory_order_acquire);
}

void Engine::captureLoop(std::stop_token stop, std::wstring deviceId) {
    ensureCom();
    raiseThreadPriority();
    OpenedClient audio;
    std::string error;
    if (!openDevice(deviceId, true, 256, static_cast<HANDLE>(captureEvent_), audio, error)) {
        {
            std::lock_guard lock(errorMu_);
            if (openError_.empty()) openError_ = error;
        }
        failed_.fetch_add(1);
        return;
    }

    const int rate = audio.pcm.sampleRate;
    if (fileOutput_) {
        sampleRate_.store(static_cast<uint32_t>(rate));
        queues_->outputRate = rate;
        queues_->resampler.setup(rate, rate);
        opened_.fetch_add(1);
    } else {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (sampleRate_.load() == 0 && !stop.stop_requested() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int outRate = sampleRate_.load();
    if (outRate <= 0) {
        audio.client->Stop();
        {
            std::lock_guard lock(errorMu_);
            if (openError_.empty()) openError_ = "The output device did not report a sample rate.";
        }
        failed_.fetch_add(1);
        return;
    }
    queues_->resampler.setup(rate, outRate);
    opened_.fetch_add(1);
    }

    std::vector<float> converted;
    std::vector<float> resampled;
    while (!stop.stop_requested()) {
        const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(captureEvent_), 500);
        if (stop.stop_requested()) {
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        UINT32 packet = 0;
        HRESULT hr = audio.capture->GetNextPacketSize(&packet);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            std::lock_guard lock(faultMu_);
            fault_ = "The microphone was disconnected.";
            faulted_.store(true, std::memory_order_release);
            break;
        }
        while (SUCCEEDED(hr) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = audio.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }
            converted.assign(static_cast<size_t>(frames) * 2, 0.f);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data) {
                toStereo(data, frames, audio.pcm, converted.data());
                if (inputMono_.load(std::memory_order_relaxed)) {
                    for (uint32_t i = 0; i < frames; ++i) {
                        const float mono = 0.5f * (converted[i * 2] + converted[i * 2 + 1]);
                        converted[i * 2] = mono;
                        converted[i * 2 + 1] = mono;
                    }
                }
                // The microphone, including while NVIDIA is on. The render thread only
                // sees samples after they leave the queue, and it does not meter while priming.
                float peak = 0.f;
                for (uint32_t i = 0; i < frames; ++i) {
                    peak = std::max(peak, std::max(std::abs(converted[i * 2]), std::abs(converted[i * 2 + 1])));
                }
                bumpPeak(inPeak_, peak);
            }
            audio.capture->ReleaseBuffer(frames);
            resampled.clear();
            queues_->resampler.process(converted.data(), frames, resampled);
            if (!resampled.empty()) {
                queues_->queue.push(resampled.data(), resampled.size() / 2);
            }
            hr = audio.capture->GetNextPacketSize(&packet);
        }
    }
    audio.client->Stop();
}

void Engine::renderLoop(std::stop_token stop, std::wstring deviceId, uint32_t requestedFrames) {
    ensureCom();
    raiseThreadPriority();
    OpenedClient audio;
    std::string error;
    if (!openDevice(deviceId, false, requestedFrames, static_cast<HANDLE>(renderEvent_), audio, error)) {
        {
            std::lock_guard lock(errorMu_);
            if (openError_.empty()) openError_ = error;
        }
        failed_.fetch_add(1);
        return;
    }
    sampleRate_.store(static_cast<uint32_t>(audio.pcm.sampleRate));
    bufferFrames_.store(audio.bufferFrames);
    queues_->outputRate = audio.pcm.sampleRate;
    opened_.fetch_add(1);

    const size_t target = std::max<size_t>(audio.bufferFrames * 2, 128);
    std::vector<float> block(static_cast<size_t>(audio.bufferFrames) * 2, 0.f);
    std::vector<float> left(audio.bufferFrames);
    std::vector<float> right(audio.bufferFrames);
    float smoothed = gain_.load(std::memory_order_relaxed);
    const float coeff = 1.f - std::exp(-1.f / (static_cast<float>(audio.pcm.sampleRate) * 0.01f));

    while (!stop.stop_requested()) {
        acknowledgeSuspend();
        const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(renderEvent_), 500);
        if (stop.stop_requested()) {
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        UINT32 padding = 0;
        HRESULT hr = audio.client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            std::lock_guard lock(faultMu_);
            fault_ = "The output device was disconnected.";
            faulted_.store(true, std::memory_order_release);
            break;
        }
        if (FAILED(hr)) {
            continue;
        }
        const UINT32 frames = audio.bufferFrames - padding;
        if (frames == 0) {
            continue;
        }
        BYTE* dest = nullptr;
        hr = audio.render->GetBuffer(frames, &dest);
        if (FAILED(hr) || !dest) {
            continue;
        }

        if (hold_.load(std::memory_order_acquire)) {
            acknowledgeSuspend();
            std::memset(dest, 0, static_cast<size_t>(frames) * audio.format->nBlockAlign);
            levels_.add(audio.pcm.sampleRate, frames, 0.f, 0.0, 0.f, 0.0);
            audio.render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
            continue;
        }

        DWORD outFlags = 0;
        const size_t available = queues_->queue.available();
        if (!queues_->primed.load() && available < target) {
            acknowledgeSuspend();
            outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
            std::memset(dest, 0, static_cast<size_t>(frames) * audio.format->nBlockAlign);
            levels_.add(audio.pcm.sampleRate, frames, 0.f, 0.0, 0.f, 0.0);
        } else {
            queues_->primed.store(true);
            if (available > target * 4) {
                queues_->queue.discardDownTo(target);
            }
            if (block.size() < static_cast<size_t>(frames) * 2) {
                block.resize(static_cast<size_t>(frames) * 2);
                left.resize(frames);
                right.resize(frames);
            }
            const size_t got = queues_->queue.pull(block.data(), frames);
            if (got < frames) {
                std::fill(block.begin() + static_cast<std::ptrdiff_t>(got * 2),
                          block.begin() + static_cast<std::ptrdiff_t>(frames * 2), 0.f);
            }
            for (UINT32 i = 0; i < frames; ++i) {
                left[i] = block[i * 2];
                right[i] = block[i * 2 + 1];
            }
            BlockLevel inLevel;
            for (UINT32 i = 0; i < frames; ++i) {
                noteSample(inLevel, left[i], right[i]);
            }
            // Before NVIDIA, so the meter stays on the microphone rather than the cleaned signal.
            bumpPeak(inPeak_, inLevel.peak);
            const bool bypassFx = acknowledgeSuspend();
            const bool ranNvidia = !bypassFx && nvidia_.active();
            if (!bypassFx) {
                filterInput(left.data(), right.data(), static_cast<int>(frames));
            }
            if (ranNvidia) {
                inLevel = {};
                for (UINT32 i = 0; i < frames; ++i) {
                    noteSample(inLevel, left[i], right[i]);
                }
            }
            if (!bypassFx && chain_) {
                constexpr int kChunk = 1024;
                UINT32 done = 0;
                while (done < frames) {
                    const int n = static_cast<int>(std::min<UINT32>(kChunk, frames - done));
                    float* stereo[2] = {left.data() + done, right.data() + done};
                    chain_->process(stereo, n);
                    done += static_cast<UINT32>(n);
                }
            }
            const float targetGain = gain_.load(std::memory_order_relaxed);
            BlockLevel outLevel;
            const int stride = audio.pcm.bytesPerSample * audio.pcm.channels;
            for (UINT32 i = 0; i < frames; ++i) {
                smoothed += (targetGain - smoothed) * coeff;
                const float l = left[i] * smoothed;
                const float r = right[i] * smoothed;
                noteSample(outLevel, l, r);
                writeFloat(dest + static_cast<size_t>(i) * stride, l, r, audio.pcm);
            }
            bumpPeak(outPeak_, outLevel.peak);
            levels_.add(audio.pcm.sampleRate, frames, inLevel.peak, inLevel.sumSq, outLevel.peak, outLevel.sumSq);
        }
        audio.render->ReleaseBuffer(frames, outFlags);
    }
    audio.client->Stop();
    acknowledgeSuspend();
    running_ = false;
}

namespace {

class WavWriter {
public:
    bool open(const std::wstring& path, int sampleRate) {
        sampleRate_ = sampleRate;
        file_.open(path, std::ios::binary | std::ios::trunc);
        if (!file_) {
            return false;
        }
        return writeHeader(0);
    }

    bool write(const float* left, const float* right, uint32_t frames) {
        pcm_.resize(static_cast<size_t>(frames) * 2);
        for (uint32_t i = 0; i < frames; ++i) {
            pcm_[static_cast<size_t>(i) * 2] = toPcm(left[i]);
            pcm_[static_cast<size_t>(i) * 2 + 1] = toPcm(right[i]);
        }
        file_.write(reinterpret_cast<const char*>(pcm_.data()),
                    static_cast<std::streamsize>(pcm_.size() * sizeof(int16_t)));
        frames_ += frames;
        return static_cast<bool>(file_);
    }

    void close() {
        if (!file_) {
            return;
        }
        writeHeader(frames_);
        file_.close();
    }

private:
    static int16_t toPcm(float sample) {
        sample = std::clamp(sample, -1.f, 1.f);
        return static_cast<int16_t>(std::lrintf(sample * 32767.f));
    }

    bool writeHeader(uint32_t frames) {
        const uint32_t dataBytes = frames * 4u;
        const uint32_t rate = static_cast<uint32_t>(sampleRate_);
        file_.seekp(0);
        file_.write("RIFF", 4);
        write32(36u + dataBytes);
        file_.write("WAVE", 4);
        file_.write("fmt ", 4);
        write32(16);
        write16(1);
        write16(2);
        write32(rate);
        write32(rate * 4u);
        write16(4);
        write16(16);
        file_.write("data", 4);
        write32(dataBytes);
        return static_cast<bool>(file_);
    }

    void write16(uint16_t value) { file_.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
    void write32(uint32_t value) { file_.write(reinterpret_cast<const char*>(&value), sizeof(value)); }

    std::ofstream file_;
    std::vector<int16_t> pcm_;
    int sampleRate_ = 48000;
    uint32_t frames_ = 0;
};

} // namespace

void Engine::fileLoop(std::stop_token stop, std::wstring path, uint32_t requestedFrames) {
    const uint32_t frames = std::max<uint32_t>(requestedFrames, 128);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (sampleRate_.load() == 0 && failed_.load() == 0 && !stop.stop_requested() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (stop.stop_requested() || failed_.load() > 0) {
        return;
    }
    const int rate = static_cast<int>(sampleRate_.load());
    if (rate <= 0) {
        std::lock_guard lock(errorMu_);
        if (openError_.empty()) {
            openError_ = "The microphone did not report a sample rate.";
        }
        failed_.fetch_add(1);
        return;
    }

    WavWriter wav;
    if (!wav.open(path, rate)) {
        std::lock_guard lock(errorMu_);
        if (openError_.empty()) {
            openError_ = "Could not create the WAV file.";
        }
        failed_.fetch_add(1);
        return;
    }
    bufferFrames_.store(frames);
    opened_.fetch_add(1);

    std::vector<float> block(static_cast<size_t>(frames) * 2, 0.f);
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    float smoothed = gain_.load(std::memory_order_relaxed);
    const float coeff = 1.f - std::exp(-1.f / (static_cast<float>(rate) * 0.01f));

    while (!stop.stop_requested()) {
        const bool held = hold_.load(std::memory_order_acquire);
        if (held || queues_->queue.available() < frames) {
            acknowledgeSuspend();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        queues_->queue.pull(block.data(), frames);
        for (uint32_t i = 0; i < frames; ++i) {
            left[i] = block[i * 2];
            right[i] = block[i * 2 + 1];
        }
        BlockLevel inLevel;
        for (uint32_t i = 0; i < frames; ++i) {
            noteSample(inLevel, left[i], right[i]);
        }
        bumpPeak(inPeak_, inLevel.peak);
        const bool bypassFx = acknowledgeSuspend();
        const bool ranNvidia = !bypassFx && nvidia_.active();
        if (!bypassFx) {
            filterInput(left.data(), right.data(), static_cast<int>(frames));
        }
        if (ranNvidia) {
            inLevel = {};
            for (uint32_t i = 0; i < frames; ++i) {
                noteSample(inLevel, left[i], right[i]);
            }
        }
        if (!bypassFx && chain_) {
            constexpr int kChunk = 1024;
            uint32_t done = 0;
            while (done < frames) {
                const int n = static_cast<int>(std::min<uint32_t>(kChunk, frames - done));
                float* stereo[2] = {left.data() + done, right.data() + done};
                chain_->process(stereo, n);
                done += static_cast<uint32_t>(n);
            }
        }
        const float targetGain = gain_.load(std::memory_order_relaxed);
        BlockLevel outLevel;
        for (uint32_t i = 0; i < frames; ++i) {
            smoothed += (targetGain - smoothed) * coeff;
            left[i] *= smoothed;
            right[i] *= smoothed;
            noteSample(outLevel, left[i], right[i]);
        }
        bumpPeak(outPeak_, outLevel.peak);
        levels_.add(rate, frames, inLevel.peak, inLevel.sumSq, outLevel.peak, outLevel.sumSq);
        if (!wav.write(left.data(), right.data(), frames)) {
            std::lock_guard lock(faultMu_);
            fault_ = "Writing the WAV file failed.";
            faulted_.store(true, std::memory_order_release);
            break;
        }
    }
    wav.close();
    acknowledgeSuspend();
    running_ = false;
}

} // namespace bv
