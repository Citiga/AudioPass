#pragma once

#include "plugins/Plugin.h"

#include <string>
#include <vector>

namespace bv {

struct SessionSlot {
    PluginInfo info;
    bool bypassed = false;
    std::string state;
};

struct Session {
    std::wstring inputId;
    std::wstring outputId;
    std::wstring outputFile;
    std::wstring levelLogDir;
    bool levelLogBinary = false;
    bool nvNoise = false;
    bool nvRoom = false;
    int nvStrength = 100;
    int levelFloorDb = 60;
    int levelTrace = 0;
    int bufferFrames = 256;
    float gainDb = 0.f;
    bool inputMono = false;
    bool topMost = false;
    bool minimizeToTray = false;
    bool closeMinimizes = false;
    bool startWithWindows = false;
    bool activateOnStart = false;
    bool hasWindow = false;
    bool windowMaximized = false;
    int windowX = 0;
    int windowY = 0;
    int windowW = 0;
    int windowH = 0;
    std::vector<std::wstring> extraFolders;
    std::vector<SessionSlot> slots;
};

Session loadSession();
void saveSession(const Session& session);
std::wstring configDirectory();

} // namespace bv
