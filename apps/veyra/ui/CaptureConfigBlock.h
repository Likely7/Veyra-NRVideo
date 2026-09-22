// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "CapturePreferenceStore.h"
#include <windows.h>
#include <functional>
#include <future>
#include <string>
#include <vector>
#include "veyra/source/CaptureCardSource.h"
namespace veyra::ui {
// Shared capture-card connection configuration block. Two hosts embed it:
// the standalone "采集卡 · 连接设置" popup and the PS5 Remote Play panel's
// combined capture mode, so both always enumerate devices, rank formats and
// persist preferences identically. Controls are created as direct children of
// the host window with ids idBase+1..idBase+19 (connect button stays with the
// host); the host forwards WM_COMMAND for those ids to handleCommand() and its
// block timer tick to poll().
class CaptureConfigBlock {
public:
    struct Callbacks {
        // readAudioIngress/setAudioIngress use engine::CaptureAudioIngress
        // values (0 automatic, 1 PCM only, 2 bitstream preferred); the mode
        // needs a reconnect because the audio media type is negotiated when
        // the capture graph is built. readFlip/setFlip toggle the manual
        // vertical ingest flip for devices whose declared DIB orientation
        // does not match their samples (RGB24 upside down).
        std::function<bool()> readSdr;std::function<bool(bool)> setSdr;
        std::function<int()> readAudioIngress;std::function<bool(int)> setAudioIngress;
        std::function<bool()> readFlip;std::function<bool(bool)> setFlip;
        std::function<int()> readBuffer;std::function<bool(int)> setBuffer;
        // Fired whenever the host should refresh its connect-button enable
        // state (query start/end, selection changes).
        std::function<void()> stateChanged;
    };
    void create(HWND parent,HFONT font,int idBase,int timerId,Callbacks callbacks);
    // Window teardown: never blocks on an in-flight enumeration; the query
    // thread owns no part of this object and its result is dropped.
    void destroy();
    // Full single-column layout (popup parity). x/y/w in DIPs.
    void arrange(int x,int y,int width);
    // Narrow vertical column for side-panel hosts (e.g. the PS5 panel's right
    // column in combined mode). Returns the block height in DIPs.
    int arrangeColumn(int x,int y,int width);
    void setVisible(bool visible);
    bool handleCommand(WPARAM wp,LPARAM lp);
    void poll();
    void refresh(){query(-1);}
    bool busy()const{return busy_;}
    bool selectionReady()const;
    // Validate the current selection, persist it as the remembered capture
    // preferences and return the capture2:/capture: path. Empty result means
    // incomplete or invalid selection; statusText() explains for the user.
    std::wstring capturePath();
    std::wstring statusText()const{return status_;}
    void setStatusText(const std::wstring& text){status_=text;if(auto h=item(8))SetWindowTextW(h,text.c_str());}
    // (id-with-base, help text) pairs for the host's installDialogHelp call.
    std::vector<std::pair<int,const wchar_t*>> helpEntries()const;
private:
    struct Query {int device=-1;std::vector<source::CaptureDevice> video,audio;std::vector<source::CaptureFormat> formats;};
    HWND item(int id)const{return GetDlgItem(parent_,idBase_+id);}
    void rebuildAudioList(int device);
    int selectedAudio(int device)const;
    void maybeShowFormatHint();
    void query(int device);
    HWND parent_=nullptr;HFONT font_=nullptr;int idBase_=0,timerId_=0;
    std::future<Query> pending_;bool busy_=false,refreshAfterQuery_=false;int queriedDevice_=-1;ULONGLONG queryStarted_=0;
    std::vector<source::CaptureFormat> formats_;
    std::vector<source::CaptureDevice> videoDevices_,audioDevices_;
    CapturePreferences remembered_;
    std::wstring status_;
    Callbacks callbacks_;
};
}
