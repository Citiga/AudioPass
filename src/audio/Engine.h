#pragma once

#include "audio/Levels.h"
#include "audio/Nvidia.h"
#include "plugins/Plugin.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bv {

inline constexpr const wchar_t kFileOutputId[] = L"bv:file";

struct DeviceInfo {
    std::wstring id;
    std::wstring name;
    bool capture = false;
    bool virtualDevice = false;
    bool fileOutput = false;
    bool isDefault = false;
};

std::vector<DeviceInfo> enumerateDevices();

class ProcessChain {
public:
    void setPlugins(std::vector<Plugin*> plugins);
    void process(float** stereo, int frames);

private:
    std::vector<Plugin*> plugins_;
};

struct EngineConfig {
    std::wstring inputId;
    std::wstring outputId;
    std::wstring outputPath;
    uint32_t bufferFrames = 256;
};

struct EngineStatus {
    uint32_t sampleRate = 0;
    uint32_t bufferFrames = 0;
    double latencyMs = 0;
};

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start(const EngineConfig& config, const std::shared_ptr<ProcessChain>& chain, std::string& error);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // The render or file thread stays out of the NVIDIA effect and the plugin chain until resume().
    void suspend();
    void resume();
    // While held, the output stays silent and plugins are not called.
    void setHold(bool hold);

    void setOutputGainDb(float db);
    void setInputMono(bool mono);
    // UI thread. Loads or releases the NVIDIA effect. While audio is running and not held,
    // the render thread is suspended for the load.
    void setNvidia(bool noise, bool room, float intensity, std::string& error);
    // UI thread. Updates the loaded effect without reloading the model.
    void setNvidiaIntensity(float intensity);
    float takeInputPeak();
    float takeOutputPeak();
    // UI thread. Copies newly published history frames, oldest first.
    int drainLevels(LevelSample* out, int count);
    // UI thread. Drops history frames that have not been copied yet.
    void discardLevels();
    EngineStatus status() const;
    // Empty when the stream is healthy. A non-empty string means the device failed.
    std::string takeFault();

private:
    struct Queues;

    void captureLoop(std::stop_token stop, std::wstring deviceId);
    void renderLoop(std::stop_token stop, std::wstring deviceId, uint32_t requestedFrames);
    void fileLoop(std::stop_token stop, std::wstring path, uint32_t requestedFrames);
    void bumpPeak(std::atomic<float>& slot, float peak);
    void filterInput(float* left, float* right, int frames);
    // Audio thread. True once this thread has observed a suspend request and is outside
    // the NVIDIA effect and the plugin chain. It will not re-enter until resume().
    bool acknowledgeSuspend();

    std::shared_ptr<ProcessChain> chain_;
    std::unique_ptr<Queues> queues_;

    std::jthread captureThread_;
    std::jthread renderThread_;
    void* captureEvent_ = nullptr;
    void* renderEvent_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<bool> hold_{true};
    // Odd values ask the audio thread to stay out of the effect and the plugin chain.
    // audioAck_ copies the request only after that thread has left both.
    std::atomic<uint32_t> suspendRequest_{0};
    std::atomic<uint32_t> audioAck_{0};
    std::atomic<bool> faulted_{false};
    std::atomic<float> gain_{1.f};
    std::atomic<bool> inputMono_{false};
    std::atomic<bool> nvidiaNoise_{false};
    std::atomic<bool> nvidiaRoom_{false};
    NvidiaFilter nvidia_;
    std::atomic<float> inPeak_{0.f};
    std::atomic<float> outPeak_{0.f};
    LevelTap levels_;
    std::atomic<uint32_t> sampleRate_{0};
    std::atomic<uint32_t> bufferFrames_{0};

    std::mutex faultMu_;
    std::string fault_;
    std::mutex errorMu_;
    std::string openError_;
    std::atomic<int> opened_{0};
    std::atomic<int> failed_{0};
    bool fileOutput_ = false;
};

} // namespace bv
