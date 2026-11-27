#pragma once
#include "veyra/engine/EnhancementSettings.h"
#include <filesystem>
#include <vector>
namespace veyra::engine {
struct UserPreset { std::wstring name; EnhancementSettings settings; };
// Strict versioned whitelist schema; no paths, devices, or executable fields.
class PresetStore {
public:
    explicit PresetStore(std::filesystem::path path)
        : path_(std::move(path)), xessSrPath_(std::filesystem::path(path_).concat(L".xess-sr")),
          fsrRcasPath_(std::filesystem::path(path_).concat(L".fsr-rcas")) {}
    bool load();
    bool save();
    bool put(std::wstring name,EnhancementSettings,bool replace=false);
    bool rename(size_t,std::wstring);
    bool erase(size_t);
    bool setDefault(size_t);
    EnhancementSettings defaultSettings()const;
    const std::vector<UserPreset>& entries()const{return entries_;}
    const std::wstring& error()const{return error_;}
private:
    static bool parse(const std::string&,std::vector<UserPreset>&,std::wstring&);
    std::string serialize()const;
    std::filesystem::path path_,xessSrPath_,fsrRcasPath_;
    std::vector<UserPreset> entries_;
    std::wstring default_,error_;
    bool corrupt_=false;
};
}
