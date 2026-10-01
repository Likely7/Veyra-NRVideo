#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace veyra::engine {
std::wstring reserveExportTemporaryFile(const std::wstring& output, std::wstring& error);
bool removeExportTemporaryFile(const std::wstring& path, std::wstring& error);
enum class ExportContainer : uint32_t { Mp4, Matroska };
enum class ExportTrackPolicy : uint32_t { Default, All, Selected, None };
// Fixed-size IPC representation. All enumerates the source without a track cap;
// only an explicit selection has a published limit (never silently truncated).
struct ExportTrackSelection {
    ExportTrackPolicy policy = ExportTrackPolicy::Default;
    uint32_t count = 0;
    std::array<int, 64> indices{};
    bool valid() const {
        if (policy > ExportTrackPolicy::None || count > indices.size()) return false;
        for (uint32_t i = 0; i < count; ++i) {
            if (indices[i] < 0) return false;
            for (uint32_t j = 0; j < i; ++j) if (indices[i] == indices[j]) return false;
        }
        return policy != ExportTrackPolicy::Selected || count > 0;
    }
    bool contains(int index) const {
        for (uint32_t i = 0; i < count; ++i) if (indices[i] == index) return true;
        return false;
    }
};
struct ExportMediaOptions {
    ExportContainer container = ExportContainer::Mp4;
    ExportTrackSelection audio;
    ExportTrackSelection subtitles{ExportTrackPolicy::None};
    // Text conversion is explicit. The UI reports it before start; bitmap
    // subtitles and incompatible audio are never silently dropped/transcoded.
    bool convertTextSubtitles = true;
    bool valid() const { return container <= ExportContainer::Matroska && audio.valid() && subtitles.valid(); }
};
}
