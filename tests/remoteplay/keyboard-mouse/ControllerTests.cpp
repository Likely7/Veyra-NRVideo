#include "veyra/remoteplay/KeyboardMouse.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace std::chrono_literals;
using namespace veyra::remoteplay;
int main(int, char** argv) try {
    std::ifstream file(std::filesystem::absolute(argv[0]).parent_path() / "profiles/gta-vi-ps5-0.6.87-calibrated.v2.json");
    std::string json{std::istreambuf_iterator<char>(file),{}};
    std::vector<ControllerState> sent;
    std::mutex mutex;
    std::condition_variable changed;
    KeyboardMouse host([&](ControllerState state) {
        std::lock_guard lock(mutex);
        sent.push_back(state);
        changed.notify_all();
    });
    size_t cursor=0;
    auto expect = [&](auto matches,const char* message) {
        std::unique_lock lock(mutex);
        if(!changed.wait_for(lock,1s,[&]{
            while(cursor<sent.size())if(matches(sent[cursor++]))return true;
            return false;
        }))throw std::runtime_error(message);
    };
    auto error=host.start(json,1600,"on_foot");
    if(!error.empty())throw std::runtime_error(error);
    host.key(0x21,true); host.key(0x21,false);
    expect([](auto s){return s.buttons & ControllerState::Triangle;},"Missing tap down");
    expect([](auto s){return !(s.buttons & ControllerState::Triangle);},"Missing tap up");
    host.motion(120,80);
    expect([](auto s){return s.rightX>0 && s.rightY>0;},"Mouse axes");
    host.button(1,true);
    expect([](auto s){return s.l2==255;},"Aim trigger");
    ControllerState pad; pad.inputActive=true; pad.buttons=ControllerState::PS;
    host.physical(pad);
    expect([](auto s){return s.l2==255;},"Quiet tick lost held trigger");
    host.stop();
    {
        std::lock_guard lock(mutex);
        for(auto& s:sent)if(s.buttons & ControllerState::PS)throw std::runtime_error("Physical ownership");
        if(sent.back()!=ControllerState{})throw std::runtime_error("Shutdown neutral");
        cursor=sent.size();
    }
    host.physical(pad);
    expect([&](auto s){return s==pad;},"Physical handoff");
    if(!host.start(json,1600,"on_foot").empty() || !host.select("driving"))throw std::runtime_error("Resume and select");
    host.key(0x11,true);
    expect([](auto s){return s.r2==255 && !s.leftY;},"Driving throttle");
    host.retire();
    if(host.start(json,1600,"on_foot").empty())throw std::runtime_error("Retired session restarted");
    std::cout<<"Ordered tap, mouse axes, trigger, ownership, quiet tick, release and manual correction passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
