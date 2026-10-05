#pragma once

#include "veyra/ngx/NgxCoreHost.h"
#include <memory>
#include <string>

namespace veyra::ngx {

// Render-thread-only, device-lifetime owner. Features and parameter blocks
// still belong to a graph; a core may be borrowed only after their release.
// Compatibility-patched FG sessions opt out until their full lifetime is shared.
class NgxCoreCache {
public:
    struct Key {
        ID3D12Device* device = nullptr;
        std::wstring runtimeDirectory;
        int nrRuntime = 0;
        bool frameGeneration = false;
        int fgBackend = 0;
        unsigned fgMultiplier = 1;
        bool operator==(const Key&) const = default;
    };
    ~NgxCoreCache();
    bool prepare(Key key, bool enabled);
    std::shared_ptr<NgxCoreHost> borrow();
    void publish(const std::shared_ptr<NgxCoreHost>& core);
    bool owns(const std::shared_ptr<NgxCoreHost>& core) const;
    bool close(const char* reason = "device-close");
private:
    Key key_;
    bool enabled_ = false;
    std::shared_ptr<NgxCoreHost> core_;
};
}
