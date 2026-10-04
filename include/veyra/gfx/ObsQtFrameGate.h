#pragma once
#include <memory>
#include <mutex>
#include <vector>

namespace veyra::gfx {
// OBS 32.1.2 dxgi-capture.cpp clears process-wide capture state at the start
// of ANY ResizeBuffers. Qt and the native video must not race to Present
// during that call. Each Qt window keeps its own gate (no UI/UI lock order).
class ObsQtFrameGate {
public:
    void beginFrame(bool hookActive) {
        if(hookActive&&!held_){mutex_.lock();held_=true;}
    }
    void endFrame() {
        if(held_){held_=false;mutex_.unlock();}
    }
    std::mutex& mutex(){return mutex_;}
private:
    std::mutex mutex_;
    bool held_=false; // accessed only by this window's render thread
};
inline std::mutex obsQtRegistryMutex;
inline std::vector<std::weak_ptr<ObsQtFrameGate>> obsQtFrameGates;
inline std::shared_ptr<ObsQtFrameGate> registerObsQtFrameGate(){
    auto gate=std::make_shared<ObsQtFrameGate>();
    const std::lock_guard lock(obsQtRegistryMutex);
    std::erase_if(obsQtFrameGates,[](const auto& existing){return existing.expired();});
    obsQtFrameGates.push_back(gate);
    return gate;
}
class ObsNativeResizeHold {
public:
    ObsNativeResizeHold(){
        {
            const std::lock_guard lock(obsQtRegistryMutex);
            for(auto& entry:obsQtFrameGates)if(auto gate=entry.lock())gates_.push_back(std::move(gate));
        }
        for(auto& gate:gates_){
            locks_.emplace_back(gate->mutex(),std::try_to_lock);
            if(!locks_.back().owns_lock()){
                locks_.clear();ready_=false;break;
            }
        }
    }
    bool ready()const{return ready_;}
    size_t windows()const{return gates_.size();}
private:
    std::vector<std::shared_ptr<ObsQtFrameGate>> gates_;
    std::vector<std::unique_lock<std::mutex>> locks_;
    bool ready_=true;
};
}
