#include "plugins/Scanner.h"

#include "Seh.h"
#include "Session.h"
#include "Utf.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <shlobj.h>
#include <windows.h>

namespace bv {
namespace {

struct ProbeRequest {
    const wchar_t* path = nullptr;
    bool vst3 = false;
    bool ok = false;
    std::string name;
    std::string vendor;
    std::string error;
    std::vector<Vst3ClassInfo> classes;
};

int probeThunk(void* user) {
    auto* request = static_cast<ProbeRequest*>(user);
    try {
        std::lock_guard lock(pluginLoadMutex());
        if (request->vst3) {
            request->ok = describeVst3File(request->path, request->classes, request->error);
        } else {
            request->ok = describeVst2File(request->path, request->name, request->vendor);
        }
    } catch (...) {
        request->ok = false;
    }
    return request->ok ? 0 : 1;
}

bool insideBundle(const std::filesystem::path& path) {
    for (std::filesystem::path cursor = path; cursor.has_parent_path(); cursor = cursor.parent_path()) {
        if (cursor.extension() == ".vst3") {
            return true;
        }
        if (cursor == cursor.root_path()) {
            break;
        }
    }
    return false;
}

int64_t stampOf(const std::filesystem::path& path) {
    std::error_code ec;
    int64_t stamp = 0;
    auto consider = [&](const std::filesystem::path& item) {
        const auto time = std::filesystem::last_write_time(item, ec);
        if (!ec) {
            stamp = std::max(stamp, static_cast<int64_t>(time.time_since_epoch().count()));
        }
    };
    consider(path);
    if (std::filesystem::is_directory(path, ec)) {
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(path, options, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 3) {
                it.disable_recursion_pending();
                continue;
            }
            consider(it->path());
        }
    }
    return stamp;
}

std::filesystem::path cacheFile() {
    return std::filesystem::path(configDirectory()) / "plugins.cache";
}

struct CacheRow {
    int64_t stamp = 0;
    PluginInfo info;
};

std::vector<CacheRow> readCache() {
    std::vector<CacheRow> rows;
    std::ifstream in(cacheFile());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::stringstream ss(line);
        std::string stamp;
        std::string format;
        std::string uid;
        std::string name;
        std::string vendor;
        std::getline(ss, stamp, '\t');
        std::getline(ss, format, '\t');
        std::getline(ss, uid, '\t');
        std::getline(ss, name, '\t');
        std::getline(ss, vendor, '\t');
        std::string path;
        std::getline(ss, path);
        if (path.empty() || stamp.empty()) {
            continue;
        }
        CacheRow row;
        row.stamp = std::stoll(stamp);
        row.info.format = format == "vst2" ? PluginFormat::Vst2 : PluginFormat::Vst3;
        row.info.uid = uid;
        row.info.name = name;
        row.info.vendor = vendor;
        row.info.path = path;
        rows.push_back(std::move(row));
    }
    return rows;
}

void writeCache(const std::vector<CacheRow>& rows) {
    std::ofstream out(cacheFile(), std::ios::trunc);
    for (const auto& row : rows) {
        out << row.stamp << '\t' << (row.info.format == PluginFormat::Vst2 ? "vst2" : "vst3") << '\t'
            << row.info.uid << '\t' << row.info.name << '\t' << row.info.vendor << '\t' << row.info.path
            << '\n';
    }
}

void addKnown(std::vector<std::wstring>& folders, REFKNOWNFOLDERID id, const wchar_t* child) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) || !raw) {
        return;
    }
    std::filesystem::path path = raw;
    CoTaskMemFree(raw);
    path /= child;
    folders.push_back(path.wstring());
}

} // namespace

Scanner::Scanner() = default;

Scanner::~Scanner() {
    shutdown();
}

std::vector<std::wstring> Scanner::defaultFolders() {
    std::vector<std::wstring> folders;
    addKnown(folders, FOLDERID_ProgramFilesCommon, L"VST3");
    addKnown(folders, FOLDERID_ProgramFilesX86, L"Common Files\\VST3");
    addKnown(folders, FOLDERID_LocalAppData, L"Programs\\Common\\VST3");
    addKnown(folders, FOLDERID_ProgramFiles, L"VSTPlugins");
    addKnown(folders, FOLDERID_ProgramFiles, L"Steinberg\\VSTPlugins");
    addKnown(folders, FOLDERID_ProgramFiles, L"Common Files\\VST2");
    addKnown(folders, FOLDERID_ProgramFilesX86, L"VSTPlugins");
    addKnown(folders, FOLDERID_ProgramFilesX86, L"Steinberg\\VSTPlugins");
    addKnown(folders, FOLDERID_ProgramFilesX86, L"Common Files\\VST2");
    return folders;
}

void Scanner::start(std::vector<std::wstring> folders, Callback onUpdate) {
    shutdown();
    stop_ = false;
    onUpdate_ = std::move(onUpdate);
    scanning_ = true;
    thread_ = std::thread([this, folders = std::move(folders)] { run(folders); });
}

void Scanner::shutdown() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    scanning_ = false;
}

std::vector<PluginInfo> Scanner::plugins() const {
    std::lock_guard lock(mu_);
    return plugins_;
}

void Scanner::run(std::vector<std::wstring> folders) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto cached = readCache();
    std::vector<CacheRow> fresh;
    std::vector<std::filesystem::path> files;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& folder : folders) {
        if (stop_) {
            break;
        }
        std::error_code ec;
        if (!std::filesystem::exists(folder, ec)) {
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(folder, options, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (stop_) {
                break;
            }
            if (it.depth() > 6) {
                it.disable_recursion_pending();
                continue;
            }
            const auto path = it->path();
            if (it->is_directory(ec) && path.extension() == ".vst3") {
                files.push_back(path);
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const auto ext = path.extension().wstring();
            if ((ext == L".dll" || ext == L".DLL") && !insideBundle(path)) {
                files.push_back(path);
            } else if (ext == L".vst3" || ext == L".VST3") {
                files.push_back(path);
            }
        }
    }

    auto publish = [&](const PluginInfo& info, int64_t stamp) {
        {
            std::lock_guard lock(mu_);
            plugins_.push_back(info);
        }
        fresh.push_back(CacheRow{stamp, info});
        if (onUpdate_) {
            onUpdate_();
        }
    };

    for (const auto& file : files) {
        if (stop_) {
            break;
        }
        const int64_t stamp = stampOf(file);
        const std::string utf8 = narrow(file.wstring());
        bool hit = false;
        for (const auto& row : cached) {
            if (row.stamp == stamp && row.info.path == utf8) {
                publish(row.info, stamp);
                hit = true;
            }
        }
        if (hit) {
            continue;
        }
        ProbeRequest request;
        const std::wstring wide = file.wstring();
        request.path = wide.c_str();
        request.vst3 = file.extension() == ".vst3";
        if (invokeSeh(&probeThunk, &request) != 0 || !request.ok) {
            continue;
        }
        if (request.vst3) {
            for (const auto& item : request.classes) {
                if (!item.effect) {
                    continue;
                }
                PluginInfo info;
                info.format = PluginFormat::Vst3;
                info.path = utf8;
                info.uid = item.uid;
                info.name = item.name;
                info.vendor = item.vendor;
                publish(info, stamp);
            }
        } else {
            PluginInfo info;
            info.format = PluginFormat::Vst2;
            info.path = utf8;
            info.name = request.name;
            info.vendor = request.vendor;
            publish(info, stamp);
        }
    }

    if (!stop_) {
        {
            std::lock_guard lock(mu_);
            std::sort(plugins_.begin(), plugins_.end(), [](const PluginInfo& a, const PluginInfo& b) {
                if (a.name != b.name) {
                    return a.name < b.name;
                }
                return a.path < b.path;
            });
        }
        std::sort(fresh.begin(), fresh.end(), [](const CacheRow& a, const CacheRow& b) {
            return a.info.name < b.info.name;
        });
        writeCache(fresh);
        if (onUpdate_) {
            onUpdate_();
        }
    }
    scanning_ = false;
    CoUninitialize();
}

} // namespace bv
