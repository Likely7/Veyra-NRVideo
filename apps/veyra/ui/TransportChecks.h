// Bounded native regression: real HWND hit testing plus the application's
// queued keyboard routing. Physical mouse/keyboard validation complements it.
bool smokeTransport=false;int transportStep=0;ULONGLONG transportTick=0;
double transportTarget=0;uint64_t transportRequest=0;
void tickTransportChecks(HWND hwnd,const veyra::engine::PlayerSnapshot& s){
    if(!smokeTransport||transportStep<0||transportStep==10||!s.frames||s.applying||transition.running)return;
    auto require=[&](bool ok,const char* label){veyra::log::info("ui-transport-test",std::format("{}={}",label,ok));if(!ok)transportStep=-1;return ok;};
    auto settled=[&]{return s.seekRequested==s.seekPresented&&std::abs(s.position-transportTarget)<.2;};
    auto hit=[&]{RECT r{};GetWindowRect(seekBar,&r);for(int n:{1,3,5,7,9})for(LONG y:{2L,(r.bottom-r.top)/2,(r.bottom-r.top)-3})if(WindowFromPoint({r.left+(r.right-r.left)*n/10,r.top+y})!=seekBar)return false;return true;};
    auto point=[&](double fraction){RECT r{};GetClientRect(seekBar,&r);return MAKELPARAM(veyra::ui::dip(seekBar,6)+int((r.right-veyra::ui::dip(seekBar,12))*fraction),r.bottom/2);};
    auto pointer=[&](UINT message,double fraction){const auto p=point(fraction);POINT screen{GET_X_LPARAM(p),GET_Y_LPARAM(p)};ClientToScreen(seekBar,&screen);SetCursorPos(screen.x,screen.y);SendMessageW(seekBar,message,message==WM_LBUTTONUP?0:MK_LBUTTON,p);};
    auto post=[&](HWND focus,WPARAM key){SetFocus(focus);PostMessageW(focus,WM_KEYDOWN,key,1);PostMessageW(focus,WM_KEYUP,key,1LL<<31);};
    switch(transportStep){
    case 0:
        ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);
        if(s.duration<60){require(false,"fixture_duration_at_least_60_seconds");break;}
        if(uiState.mode==veyra::ui::Mode::Daily){switchMode();return;}
        engine.pause(true);paused=true;SetWindowTextW(GetDlgItem(hwnd,Play),L"Play");
        showDiagnostics=true;layout();SetFocus(GetDlgItem(hwnd,Volume));if(!full)toggleFullscreen();
        transportTick=GetTickCount64();transportStep=1;break;
    case 1:
        if(GetTickCount64()-transportTick<300)return;
        pointerActivity();
        if(!require(GetFocus()==hwnd,"fullscreen_reclaims_playback_focus")||!require(hit(),"professional_fullscreen_seek_entire_hitbox"))break;
        transportRequest=s.seekRequested;
        pointer(WM_LBUTTONDOWN,.2);
        pointer(WM_MOUSEMOVE,.4);
        transportTick=GetTickCount64();transportStep=2;break;
    case 2:
        if(GetTickCount64()-transportTick<1900)return;
        if(!require(fullControls&&dragging&&GetCapture()==seekBar&&s.seekPresented>transportRequest&&s.seekPresented==s.seekRequested&&std::abs(s.position-s.duration*.4)<s.duration*.001,"drag_presents_latest_frame_while_mouse_still_held"))break;
        pointer(WM_LBUTTONUP,.4);
        transportTarget=engine.snapshot().seekTarget;transportStep=3;break;
    case 3:
        if(!settled())return;
        if(!require(std::abs(transportTarget-s.duration*.4)<s.duration*.001&&!dragging&&!GetCapture()&&s.transport==veyra::engine::TransportState::Paused,"drag_release_presents_requested_frame_and_preserves_pause"))break;
        transportTarget=s.position+10;post(GetDlgItem(hwnd,Volume),VK_RIGHT);transportStep=4;break;
    case 4:
        if(!settled())return;
        if(!require(true,"fullscreen_right_adds_10_seconds_with_volume_focus"))break;
        fullControls=false;layout();transportTarget=s.position-10;post(hwnd,VK_LEFT);transportStep=5;break;
    case 5:
        if(!settled())return;
        if(!require(fullControls,"hidden_controls_left_subtracts_10_seconds_and_reveals_bar"))break;
        toggleFullscreen();showDiagnostics=false;layout();transportRequest=s.seekRequested;
        post(GetDlgItem(hwnd,Volume),VK_RIGHT);transportTick=GetTickCount64();transportStep=6;break;
    case 6:
        if(GetTickCount64()-transportTick<300)return;
        if(!require(s.seekRequested==transportRequest,"windowed_volume_arrows_do_not_seek"))break;
        transportTarget=s.position+10;post(video,VK_RIGHT);transportStep=7;break;
    case 7:
        if(!settled())return;
        require(true,"windowed_video_right_adds_10_seconds");
        if(uiState.mode!=veyra::ui::Mode::Daily)switchMode();transportStep=8;break;
    case 8:
        if(!full)toggleFullscreen();pointerActivity();
        if(!require(hit(),"daily_fullscreen_seek_entire_hitbox"))break;
        pointer(WM_LBUTTONDOWN,.2);pointer(WM_LBUTTONUP,.2);
        transportTarget=engine.snapshot().seekTarget;transportStep=9;break;
    case 9:
        if(!settled())return;
        if(!require(std::abs(transportTarget-s.duration*.2)<s.duration*.001,"daily_fullscreen_click_presents_requested_frame"))break;
        engine.pause(false);paused=false;transportStep=11;break;
    case 11:
        if(s.transport!=veyra::engine::TransportState::Playing)return;
        transportRequest=s.seekPresented;
        pointer(WM_LBUTTONDOWN,.3);pointer(WM_MOUSEMOVE,.5);
        transportTick=GetTickCount64();transportStep=12;break;
    case 12:
        if(GetTickCount64()-transportTick<1900)return;
        if(!require(dragging&&s.seekPresented>transportRequest&&std::abs(s.position-s.duration*.5)<s.duration*.001&&s.transport==veyra::engine::TransportState::Paused,"playing_drag_shows_frame_with_transport_temporarily_paused"))break;
        pointer(WM_LBUTTONUP,.6);transportTarget=engine.snapshot().seekTarget;transportStep=13;break;
    case 13:
        // The timer's snapshot precedes SeekPreview::tick(), which may have
        // just resumed playback. Observe the next snapshot before asserting.
        if(s.seekRequested!=s.seekPresented||seekPreview.active||s.transport!=veyra::engine::TransportState::Playing)return;
        if(!require(s.transport==veyra::engine::TransportState::Playing&&s.position>=transportTarget-.1&&s.position<transportTarget+1,"release_supersedes_preview_and_resumes_playback"))break;
        transportStep=10;veyra::log::info("ui-transport-test","PASS live drag preview, exact release, pause/resume, focus, queued arrows, windowed slider isolation");break;
    }
}
