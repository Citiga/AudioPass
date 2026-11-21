#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace bv {

struct NvidiaSupport {
    bool sdk = false;
    bool noise = false;
    bool room = false;
};

// Looks for the NVIDIA Audio Effects redistributable. Does not load the DLL.
NvidiaSupport probeNvidia();

// Noise removal and room echo removal. The DLL is loaded only when an effect is opened,
// so AudioPass still runs when the SDK is not installed.
// open() and close() run on the UI thread. close() waits until process() has returned.
// process() runs on the audio thread. It returns 0, or an NvAFX_Run error after silencing the block.
class NvidiaFilter {
public:
    NvidiaFilter() = default;
    ~NvidiaFilter();

    NvidiaFilter(const NvidiaFilter&) = delete;
    NvidiaFilter& operator=(const NvidiaFilter&) = delete;

    bool open(int deviceRate, bool noise, bool room, std::string& error);
    void close();
    bool active() const { return active_.load(std::memory_order_acquire); }
    // 0 leaves the signal unchanged, 1 is the full effect. Safe to call while process() runs.
    void setIntensity(float ratio);
    // 0 on success, or when the effect is not open. A non-zero value is an NvAFX_Run failure
    // and the block has been silenced.
    int process(float* left, float* right, int frames);

private:
    struct Api;
    struct Stage;
    struct Shift;

    bool loadApi(std::string& error);
    bool openStage(Stage& stage, const char* effect, const std::wstring& model, int modelRate, std::string& error);
    void destroyStage(Stage& stage);
    int pushChain(const float* samples, int count);
    void applyIntensity();

    Api* api_ = nullptr;
    Stage* room_ = nullptr;
    Stage* noise_ = nullptr;
    Shift* toModel_ = nullptr;
    Shift* toDevice_ = nullptr;
    int deviceRate_ = 0;
    int modelRate_ = 0;
    std::atomic<bool> active_{false};
    // process() calls on the stack. close() waits for zero before it frees an effect.
    std::atomic<int> busy_{0};
    // Sticky NvAFX_Run status. 0 is success. Cleared by close().
    std::atomic<int> runStatus_{0};
    std::atomic<int> intensityPercent_{100};
    int appliedPercent_ = -1;
    std::vector<float> out_;
    size_t outRead_ = 0;
};
} // namespace bv
