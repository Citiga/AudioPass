#include "ui/LevelFile.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace bv {
namespace {

constexpr char kMagic[8] = {'A', 'P', 'L', 'E', 'V', 'E', 'L', '1'};
constexpr uint32_t kVersion = 1;

void writeU32(std::ofstream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool readU32(std::ifstream& in, uint32_t& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(in);
}

bool readSample(std::ifstream& in, LevelSample& sample) {
    float values[4];
    in.read(reinterpret_cast<char*>(values), sizeof(values));
    if (!in) {
        return false;
    }
    sample.inPeakDb = values[0];
    sample.inRmsDb = values[1];
    sample.outPeakDb = values[2];
    sample.outRmsDb = values[3];
    return true;
}

} // namespace

LevelLogWriter::~LevelLogWriter() {
    close();
}

bool LevelLogWriter::open(const std::wstring& path, bool binary, std::string& error) {
    close();
    auto* out = new std::ofstream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!*out) {
        delete out;
        error = "Could not create the level history file.";
        return false;
    }
    binary_ = binary;
    count_ = 0;
    path_ = path;
    file_ = out;
    if (binary_) {
        out->write(kMagic, sizeof(kMagic));
        writeU32(*out, kVersion);
        writeU32(*out, static_cast<uint32_t>(kLevelBinsPerSecond));
        writeU32(*out, 0);
        if (!*out) {
            error = "Could not write the level history file.";
            close();
            return false;
        }
    } else {
        *out << "t_s,in_peak_db,in_rms_db,out_peak_db,out_rms_db\n";
    }
    return true;
}

void LevelLogWriter::write(const LevelSample* samples, int count) {
    auto* out = static_cast<std::ofstream*>(file_);
    if (out == nullptr || samples == nullptr || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        const LevelSample& sample = samples[i];
        if (binary_) {
            const float values[4] = {sample.inPeakDb, sample.inRmsDb, sample.outPeakDb, sample.outRmsDb};
            out->write(reinterpret_cast<const char*>(values), sizeof(values));
        } else {
            const double seconds = static_cast<double>(count_) / static_cast<double>(kLevelBinsPerSecond);
            char line[128];
            std::snprintf(line, sizeof(line), "%.3f,%.1f,%.1f,%.1f,%.1f\n", seconds, static_cast<double>(sample.inPeakDb),
                          static_cast<double>(sample.inRmsDb), static_cast<double>(sample.outPeakDb),
                          static_cast<double>(sample.outRmsDb));
            *out << line;
        }
        ++count_;
    }
    out->flush();
}

void LevelLogWriter::close() {
    auto* out = static_cast<std::ofstream*>(file_);
    if (out == nullptr) {
        return;
    }
    if (binary_ && *out) {
        out->seekp(16);
        writeU32(*out, count_);
    }
    out->flush();
    out->close();
    delete out;
    file_ = nullptr;
    count_ = 0;
    binary_ = false;
}

bool LevelLogWriter::isOpen() const {
    const auto* out = static_cast<const std::ofstream*>(file_);
    return out != nullptr && out->is_open();
}

bool LoadLevelHistory(const std::wstring& path, std::vector<LevelSample>& samples, int& binsPerSecond, std::string& error) {
    samples.clear();
    binsPerSecond = kLevelBinsPerSecond;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) {
        error = "Could not open the level history file.";
        return false;
    }
    char magic[8] = {};
    in.read(magic, sizeof(magic));
    if (in && std::memcmp(magic, kMagic, sizeof(kMagic)) == 0) {
        uint32_t version = 0;
        uint32_t rate = 0;
        uint32_t count = 0;
        if (!readU32(in, version) || !readU32(in, rate) || !readU32(in, count) || version != kVersion || rate == 0) {
            error = "The level history file is not a supported binary recording.";
            return false;
        }
        binsPerSecond = static_cast<int>(rate);
        if (count > 0) {
            samples.reserve(count);
        }
        LevelSample sample;
        while (readSample(in, sample)) {
            samples.push_back(sample);
            if (count > 0 && samples.size() >= count) {
                break;
            }
        }
    } else {
        in.clear();
        in.seekg(0);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line[0] == 't' || line[0] == '#') {
                continue;
            }
            float values[5] = {};
            const char* cursor = line.c_str();
            bool parsed = true;
            for (float& value : values) {
                char* end = nullptr;
                value = std::strtof(cursor, &end);
                if (end == cursor) {
                    parsed = false;
                    break;
                }
                cursor = end;
                if (&value != &values[4]) {
                    if (*cursor != ',') {
                        parsed = false;
                        break;
                    }
                    ++cursor;
                }
            }
            if (!parsed) {
                continue;
            }
            LevelSample sample;
            sample.inPeakDb = values[1];
            sample.inRmsDb = values[2];
            sample.outPeakDb = values[3];
            sample.outRmsDb = values[4];
            samples.push_back(sample);
        }
    }
    if (samples.empty()) {
        error = "The level history file has no samples.";
        return false;
    }
    return true;
}

} // namespace bv
