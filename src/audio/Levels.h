#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace bv {

// How often the audio thread publishes one history frame, and how much of that
// the graph keeps. 20 Hz for 50 s is 1000 frames.
inline constexpr int kLevelBinsPerSecond = 20;
inline constexpr int kLevelHistorySeconds = 50;

struct LevelSample {
    float inPeakDb = -140.f;
    float inRmsDb = -140.f;
    float outPeakDb = -140.f;
    float outRmsDb = -140.f;
};

// Lock-free handoff from the audio thread to the UI.
//
// One thread publishes (the render thread, or the file thread when the output
// is a WAV). It folds each block into a peak and an RMS on both sides of the
// plugin chain, and about every 50 ms stores one frame. The UI thread is the
// only reader. A frame is one 64-bit store, so it is never torn and the audio
// thread never takes a lock.
class LevelTap {
public:
    LevelTap();
    LevelTap(const LevelTap&) = delete;
    LevelTap& operator=(const LevelTap&) = delete;

    // Audio thread. `inSumSq` / `outSumSq` sum the louder channel's square each frame.
    // Peak is the loudest |sample| on either channel.
    void add(int sampleRate, uint32_t frames, float inPeak, double inSumSq, float outPeak, double outSumSq);
    // Audio thread must not be inside add(). Clears the open bin only.
    void resetBin();

    // UI thread. Copies newly published frames, oldest first.
    int drain(LevelSample* out, int count);
    // UI thread. Forgets frames that have not been drained yet.
    void discard();

private:
    void publish(float inPeakDb, float inRmsDb, float outPeakDb, float outRmsDb);

    // One slot stays empty so the reader never touches the slot being written.
    static constexpr uint32_t kCapacity = 1024;
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");
    static_assert(kCapacity - 1 >= static_cast<uint32_t>(kLevelHistorySeconds * kLevelBinsPerSecond));
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "a level frame must publish with one atomic store");

    std::array<std::atomic<uint64_t>, kCapacity> slots_{};
    std::atomic<uint32_t> write_{0};
    uint32_t read_ = 0;

    int rate_ = 0;
    int window_ = 1;
    uint32_t filled_ = 0;
    float inPeak_ = 0.f;
    float outPeak_ = 0.f;
    double inSumSq_ = 0.0;
    double outSumSq_ = 0.0;
};

} // namespace bv
