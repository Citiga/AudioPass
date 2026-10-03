#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace bv {

enum class PluginFormat { Vst2, Vst3 };

struct PluginInfo {
    PluginFormat format = PluginFormat::Vst3;
    std::string path;
    std::string uid;
    std::string name;
    std::string vendor;
};

class Plugin {
public:
    virtual ~Plugin() = default;

    virtual const PluginInfo& info() const = 0;
    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void process(float** stereo, int frames) = 0;
    virtual void setBypassed(bool bypassed) = 0;
    virtual bool bypassed() const = 0;
    virtual bool hasEditor() const = 0;
    virtual bool editorResizable() const { return false; }
    virtual int parameterCount() const = 0;
    virtual std::string parameterName(int index) const = 0;
    virtual float parameterValue(int index) const = 0;
    virtual void setParameterValue(int index, float normalized) = 0;
    virtual bool openEditor(void* parentHwnd, int& width, int& height,
                            std::function<void(int, int)> resize) = 0;
    virtual void closeEditor() = 0;
    virtual void resizeEditor(int width, int height) { (void)width; (void)height; }
    virtual void idleEditor() = 0;
    virtual std::string saveState() const = 0;
    virtual void restoreState(std::string_view data) = 0;
};

std::mutex& pluginLoadMutex();

std::unique_ptr<Plugin> loadVst2Plugin(const PluginInfo& info, std::string& error);
std::unique_ptr<Plugin> loadVst3Plugin(const PluginInfo& info, std::string& error);

inline std::unique_ptr<Plugin> loadPlugin(const PluginInfo& info, std::string& error) {
    std::lock_guard lock(pluginLoadMutex());
    if (info.format == PluginFormat::Vst2) {
        return loadVst2Plugin(info, error);
    }
    return loadVst3Plugin(info, error);
}

struct Vst3ClassInfo {
    std::string uid;
    std::string name;
    std::string vendor;
    bool effect = true;
};

bool describeVst2File(const std::wstring& path, std::string& name, std::string& vendor);
bool describeVst3File(const std::wstring& path, std::vector<Vst3ClassInfo>& classes, std::string& error);

} // namespace bv
