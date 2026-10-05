#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace veyra::gfx {
// Qt 6.8.3 resizes BEFORE beforeFrameBegin; OBS 32.1.2 clears its single
// process-wide capture on ANY ResizeBuffers. Require a completed Qt frame
// at the current window extent, then keep Qt Presents outside native resize.
class ObsQtFrameGate {
public:
    explicit ObsQtFrameGate(HWND window):window_(window){}
    void beginFrame(bool hookActive){
        if(hookActive&&!held_){
            mutex_.lock();held_=true;presented_=false;frameExtent_=0;
        }
    }
    void presented(unsigned width,unsigned height){
        if(held_&&width&&height){frameExtent_=(uint64_t(width)<<32)|height;presented_=true;}
    }
    void endFrame(){
        if(held_){
            if(presented_)completedExtent_.store(frameExtent_,std::memory_order_release);
            abortFrame();
        }
    }
    void abortFrame(){if(held_){held_=false;mutex_.unlock();}}
    bool readyForCurrentWindow()const{
        if(!IsWindow(window_)||!IsWindowVisible(window_)||IsIconic(window_))return true;
        const auto extent=clientExtent();
        return extent!=0&&completedExtent_.load(std::memory_order_acquire)==extent;
    }
    std::mutex& mutex(){return mutex_;}
private:
    uint64_t clientExtent()const{
        RECT rc{};
        if(!GetClientRect(window_,&rc)||rc.right<1||rc.bottom<1)return 0;
        return (uint64_t(uint32_t(rc.right))<<32)|uint32_t(rc.bottom);
    }
    HWND window_=nullptr;
    std::mutex mutex_;
    std::atomic<uint64_t> completedExtent_{0};
    uint64_t frameExtent_=0;
    bool held_=false,presented_=false; // this window's render thread only
};
inline std::mutex obsQtRegistryMutex;
inline std::vector<std::weak_ptr<ObsQtFrameGate>> obsQtFrameGates;
inline std::shared_ptr<ObsQtFrameGate> registerObsQtFrameGate(HWND window){
    auto gate=std::make_shared<ObsQtFrameGate>(window);
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
            if(!locks_.back().owns_lock()||!gate->readyForCurrentWindow()){
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
