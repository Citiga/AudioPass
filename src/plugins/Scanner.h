#pragma once

#include "plugins/Plugin.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bv {

class Scanner {
public:
    using Callback = std::function<void()>;

    Scanner();
    ~Scanner();

    void start(std::vector<std::wstring> folders, Callback onUpdate);
    void shutdown();
    bool scanning() const { return scanning_.load(std::memory_order_acquire); }
    std::vector<PluginInfo> plugins() const;

    static std::vector<std::wstring> defaultFolders();

private:
    void run(std::vector<std::wstring> folders);

    mutable std::mutex mu_;
    std::vector<PluginInfo> plugins_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> scanning_{false};
    Callback onUpdate_;
};

} // namespace bv
