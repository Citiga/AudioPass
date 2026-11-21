#pragma once

#include "audio/Levels.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bv {

// Streamed level recording. CSV is text. Binary is little-endian:
// 8 bytes "APLEVEL1", then uint32 version, bins-per-second, and sample count,
// then count groups of four floats (in peak, in RMS, out peak, out RMS).
class LevelLogWriter {
public:
    LevelLogWriter() = default;
    ~LevelLogWriter();
    LevelLogWriter(const LevelLogWriter&) = delete;
    LevelLogWriter& operator=(const LevelLogWriter&) = delete;

    bool open(const std::wstring& path, bool binary, std::string& error);
    void write(const LevelSample* samples, int count);
    void close();
    bool isOpen() const;
    const std::wstring& path() const { return path_; }

private:
    std::wstring path_;
    bool binary_ = false;
    uint32_t count_ = 0;
    void* file_ = nullptr;
};

bool LoadLevelHistory(const std::wstring& path, std::vector<LevelSample>& samples, int& binsPerSecond, std::string& error);

} // namespace bv
