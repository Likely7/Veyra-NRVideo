#include "veyra/ngx/NgxCoreCache.h"
#include "veyra/Log.h"
#include <format>
#include <utility>

namespace veyra::ngx {
NgxCoreCache::~NgxCoreCache() { (void)close(); }

bool NgxCoreCache::close(const char* reason) {
    if (!core_) return true;
    if (core_.use_count() != 1) {
        log::error("ngx-core-cache", std::format("event=refused-close reason={} borrowers={}", reason, core_.use_count()-1));
        return false;
    }
    log::info("ngx-core-cache", std::format("event=close reason={} liveParameters={}", reason, core_->liveParameterBlockCount()));
    core_->shutdown();
    core_.reset();
    return true;
}

bool NgxCoreCache::prepare(Key key, bool enabled) {
    // Prevent a second NGX Init on this device while a graph still uses it.
    if (core_ && core_.use_count() != 1) {
        log::error("ngx-core-cache", "event=refused-prepare active graph must release its core first");
        return false;
    }
    if ((!enabled || !(key == key_)) && !close(enabled ? "key-changed" : "disabled-or-compatibility")) return false;
    key_ = std::move(key);
    enabled_ = enabled;
    return true;
}

std::shared_ptr<NgxCoreHost> NgxCoreCache::borrow() {
    if (!enabled_ || !core_) return {};
    if (!core_->healthy() || core_->liveParameterBlockCount() != 0 ||
        !key_.device || FAILED(key_.device->GetDeviceRemovedReason())) {
        (void)close("invalid-core-or-parameters");
        return {};
    }
    log::info("ngx-core-cache", "event=hit liveParameters=0");
    return core_;
}

void NgxCoreCache::publish(const std::shared_ptr<NgxCoreHost>& core) {
    if (!enabled_ || !core || !core->healthy()) return;
    if (core_ && core_ != core) {
        log::error("ngx-core-cache", "event=refused-publish different core already retained");
        return;
    }
    if (!core_) log::info("ngx-core-cache", "event=publish initialized graph owns all live parameters");
    core_ = core;
}

bool NgxCoreCache::owns(const std::shared_ptr<NgxCoreHost>& core) const {
    return enabled_ && core && core_ == core;
}
}
