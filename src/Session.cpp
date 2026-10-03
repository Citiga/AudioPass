#include "Session.h"

#include "Utf.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>

namespace bv {
namespace {

std::filesystem::path configPath() {
    PWSTR raw = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) && raw) {
        path = raw;
        CoTaskMemFree(raw);
    } else {
        path = std::filesystem::temp_directory_path();
    }
    path /= "AudioPass";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path / "session.cfg";
}

std::string base64Encode(std::string_view data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char byte : data) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kTable[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) {
        out.push_back(kTable[(buffer << (6 - bits)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

std::string base64Decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\r' || c == '\n') {
            continue;
        }
        const int v = value(c);
        if (v < 0) {
            continue;
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string rest(const std::string& line, size_t prefix) {
    return line.size() > prefix ? line.substr(prefix) : std::string{};
}

} // namespace

std::wstring configDirectory() {
    return configPath().parent_path().wstring();
}

Session loadSession() {
    Session session;
    std::ifstream in(configPath());
    if (!in) {
        return session;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("input=", 0) == 0) {
            session.inputId = widen(rest(line, 6));
        } else if (line.rfind("output=", 0) == 0) {
            session.outputId = widen(rest(line, 7));
        } else if (line.rfind("file=", 0) == 0) {
            session.outputFile = widen(rest(line, 5));
        } else if (line.rfind("buffer=", 0) == 0) {
            session.bufferFrames = std::stoi(rest(line, 7));
        } else if (line.rfind("gain=", 0) == 0) {
            session.gainDb = std::stof(rest(line, 5));
        } else if (line.rfind("mono=", 0) == 0) {
            session.inputMono = rest(line, 5) == "1";
        } else if (line.rfind("topmost=", 0) == 0) {
            session.topMost = rest(line, 8) == "1";
        } else if (line.rfind("minimizeTray=", 0) == 0) {
            session.minimizeToTray = rest(line, 13) == "1";
        } else if (line.rfind("closeMinimizes=", 0) == 0) {
            session.closeMinimizes = rest(line, 15) == "1";
        } else if (line.rfind("startWithWindows=", 0) == 0) {
            session.startWithWindows = rest(line, 17) == "1";
        } else if (line.rfind("activateOnStart=", 0) == 0) {
            session.activateOnStart = rest(line, 16) == "1";
        } else if (line.rfind("window=", 0) == 0) {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            int maximized = 0;
            if (sscanf_s(rest(line, 7).c_str(), "%d,%d,%d,%d,%d", &x, &y, &w, &h, &maximized) >= 4 && w >= 80 &&
                h >= 80) {
                session.windowX = x;
                session.windowY = y;
                session.windowW = w;
                session.windowH = h;
                session.windowMaximized = maximized != 0;
                session.hasWindow = true;
            }
        } else if (line.rfind("folder=", 0) == 0) {
            session.extraFolders.push_back(widen(rest(line, 7)));
        } else if (line.rfind("slot=", 0) == 0) {
            std::stringstream ss(rest(line, 5));
            std::string format;
            std::string bypass;
            std::string uid;
            std::getline(ss, format, '|');
            std::getline(ss, bypass, '|');
            std::getline(ss, uid, '|');
            std::string path;
            std::getline(ss, path);
            SessionSlot slot;
            slot.info.format = format == "vst2" ? PluginFormat::Vst2 : PluginFormat::Vst3;
            slot.info.uid = uid;
            slot.info.path = path;
            slot.bypassed = bypass == "1";
            if (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.rfind("state=", 0) == 0 && line != "state=-") {
                    slot.state = base64Decode(rest(line, 6));
                }
            }
            session.slots.push_back(std::move(slot));
        }
    }
    if (session.bufferFrames != 128 && session.bufferFrames != 256 && session.bufferFrames != 512) {
        session.bufferFrames = 256;
    }
    return session;
}

void saveSession(const Session& session) {
    std::ofstream out(configPath(), std::ios::trunc);
    if (!out) {
        return;
    }
    out << "input=" << narrow(session.inputId) << "\n";
    out << "output=" << narrow(session.outputId) << "\n";
    out << "file=" << narrow(session.outputFile) << "\n";
    out << "buffer=" << session.bufferFrames << "\n";
    out << "gain=" << session.gainDb << "\n";
    out << "mono=" << (session.inputMono ? "1" : "0") << "\n";
    out << "topmost=" << (session.topMost ? "1" : "0") << "\n";
    out << "minimizeTray=" << (session.minimizeToTray ? "1" : "0") << "\n";
    out << "closeMinimizes=" << (session.closeMinimizes ? "1" : "0") << "\n";
    out << "startWithWindows=" << (session.startWithWindows ? "1" : "0") << "\n";
    out << "activateOnStart=" << (session.activateOnStart ? "1" : "0") << "\n";
    if (session.hasWindow) {
        out << "window=" << session.windowX << "," << session.windowY << "," << session.windowW << ","
            << session.windowH << "," << (session.windowMaximized ? 1 : 0) << "\n";
    }
    for (const auto& folder : session.extraFolders) {
        out << "folder=" << narrow(folder) << "\n";
    }
    for (const auto& slot : session.slots) {
        out << "slot=" << (slot.info.format == PluginFormat::Vst2 ? "vst2" : "vst3") << "|"
            << (slot.bypassed ? "1" : "0") << "|" << slot.info.uid << "|" << slot.info.path << "\n";
        if (slot.state.empty()) {
            out << "state=-\n";
        } else {
            out << "state=" << base64Encode(slot.state) << "\n";
        }
    }
}

} // namespace bv
