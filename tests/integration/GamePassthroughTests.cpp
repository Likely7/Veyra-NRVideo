#include "veyra/ui/GamePassthrough.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <iostream>

using namespace veyra::ui;
namespace {
int failures=0, panels=0, exits=0;
GameHotkeyListener* listener=nullptr;
void check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failures+=!ok;}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    if(m==GameHotkeyListener::message){const bool current=listener&&listener->current(lp);if(current){if(wp==0)++panels;else if(wp==1)++exits;}
        std::cout<<"EVENT action="<<wp<<" epoch="<<lp<<" current="<<current<<" panels="<<panels<<" exits="<<exits<<std::endl;return 0;}
    return DefWindowProcW(w,m,wp,lp);
}
void pump(unsigned ms){const auto end=GetTickCount64()+ms;do{QCoreApplication::processEvents();MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount64()<end);}
bool key(WORD vk,bool up=false){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;return SendInput(1,&i,sizeof(i))==1;}
bool send(std::initializer_list<std::pair<WORD,bool>> keys){
    // Do not block the receiving window's message pump inside SendInput. In
    // particular, unclaimed input must be able to finish the native hook chain.
    bool ok=true;std::atomic<bool> done{false};
    std::thread sender([&]{for(auto [vk,up]:keys)ok=key(vk,up)&&ok;done.store(true);});
    while(!done.load())pump(1);sender.join();pump(20);return ok;
}
void tap(WORD vk){check(send({{vk,false},{vk,true}}),"SendInput reaches our test foreground");pump(80);}
void quickWindowSequence(HWND target){
    QQuickWindow host;
    host.setGeometry(0,0,960,600);
    host.show();pump(100);
    GamePassthrough game;
    game.attach(reinterpret_cast<HWND>(host.winId()));
    game.setTarget(target,MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),false);
    game.setHotkeys({VK_F9,0},{VK_F10,3});
    int panelEvents=0,exitEvents=0;
    const auto apply=[&](GameInputMode mode){
        Qt::WindowFlags flags=Qt::Window|Qt::FramelessWindowHint;
        if(mode==GameInputMode::Through)flags|=Qt::WindowTransparentForInput;
        if(mode==GameInputMode::Through||mode==GameInputMode::Scaled)flags|=Qt::WindowDoesNotAcceptFocus;
        host.setFlags(flags);
        if(mode==GameInputMode::Off)host.showNormal();else host.showFullScreen();
        game.attach(reinterpret_cast<HWND>(host.winId()));
        game.apply(mode);
    };
    game.onEvent=[&](GamePassthrough::Event event){
        if(event==GamePassthrough::Event::PanelHotkey){
            ++panelEvents;apply(game.mode()==GameInputMode::Panel?GameInputMode::Through:GameInputMode::Panel);
        }else if(event==GamePassthrough::Event::ExitHotkey){++exitEvents;apply(GameInputMode::Off);}
        else {check(false,"quick window listener remains available");apply(GameInputMode::Off);}
    };
    ShowWindow(target,SW_SHOW);
    check(game.arm(),"message-only receiver arms before fullscreen flag changes");
    apply(GameInputMode::Through);pump(100);
    check(GetForegroundWindow()==target,"Qt fullscreen gives foreground to capture target");
    for(int i=0;i<3;++i){
        tap(VK_F9);
        check(game.mode()==GameInputMode::Panel,"fullscreen hotkey restores interactive panel");
        SetWindowPos(target,nullptr,420,300,720+i*20,400+i*30,SWP_NOACTIVATE|SWP_NOZORDER);
        const auto geometry=game.pictureBounds();RECT source{};
        DwmGetWindowAttribute(target,DWMWA_EXTENDED_FRAME_BOUNDS,&source,sizeof(source));
        check(geometry.right-geometry.left==source.right-source.left&&geometry.bottom-geometry.top==source.bottom-source.top,
              "source resize is reflected while passthrough is paused");
        tap(VK_F9);
        check(game.mode()==GameInputMode::Through,"panel closes back into passthrough");
    }
    check(panelEvents==6,"Qt flag transitions preserve all panel hotkey events");
    apply(GameInputMode::Scaled);pump(80);
    const auto scaledStyle=GetWindowLongPtrW(reinterpret_cast<HWND>(host.winId()),GWL_EXSTYLE);
    check(!(scaledStyle&WS_EX_TRANSPARENT)&&(scaledStyle&WS_EX_NOACTIVATE)&&GetForegroundWindow()==target,
          "scaled output keeps keyboard/gamepad foreground and blocks incorrect mouse passthrough");
    // Force a platform-window replacement rather than assuming this Qt backend
    // always replaces its HWND for an input-flag change.
    host.destroy();host.create();
    apply(GameInputMode::Through);pump(100);
    check(send({{VK_LCONTROL,false},{VK_LMENU,false},{VK_F10,false},{VK_F10,true},{VK_LMENU,true},{VK_LCONTROL,true}}),
          "exit chord reaches listener after platform-window replacement");pump(100);
    const auto restoredStyle=GetWindowLongPtrW(reinterpret_cast<HWND>(host.winId()),GWL_EXSTYLE);
    check(exitEvents==1&&game.mode()==GameInputMode::Off&&!(restoredStyle&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)),
          "exit restores real native input styles after fullscreen and handle changes");
    game.onEvent={};game.apply(GameInputMode::Off);host.hide();
}
}
int main(int argc,char** argv){
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
    check(gameKey(QKeySequence("Home"))==GameKey{VK_HOME,0},"default panel maps to Home");
    check(gameKey(QKeySequence("Ctrl+Alt+Home"))==GameKey{VK_HOME,3},"default exit maps to Ctrl Alt Home");
    for(const auto* invalid:{"Ctrl", "Meta+F9", "F9, F10", "UnknownKey", "Ctrl++"})check(gameKey(QKeySequence(invalid)).vk==0,"unsupported bindings cannot arm listener");
    GameKeyState state;const GameKey panel{VK_HOME,0},exit{VK_HOME,3};
    auto input=[&](unsigned vk,bool down,bool owned=true){return state.input(vk,down,panel,exit,owned);};
    check(!input('A',true).consume&&!input('A',false).consume,"ordinary typing passes through");
    check(!input(VK_HOME,true,false).consume&&!input(VK_HOME,false,false).consume,"other programs retain panel key");
    check(input(VK_HOME,true).action==0&&input(VK_HOME,true).action<0,"held panel key toggles only once");
    check(input(VK_HOME,false).consume,"claimed key release is paired");
    input(VK_LCONTROL,true);input(VK_RCONTROL,true);input(VK_LCONTROL,false);input(VK_RMENU,true);
    check(input(VK_HOME,true).action==1,"right Ctrl remains held after left Ctrl release");
    check(input(VK_HOME,false).consume,"exit key release is paired");
    input(VK_RCONTROL,false);input(VK_RMENU,false);input(VK_LSHIFT,true);
    check(!input(VK_HOME,true).consume,"extra Shift does not trigger plain Home");input(VK_HOME,false);input(VK_LSHIFT,false);
    GameKeyState changed;changed.input(VK_F9,true,{VK_F9,0},{VK_F10,3},false);
    changed.input(VK_F9,false,{VK_F8,0},{VK_F10,3},false);
    check(changed.input(VK_F9,true,{VK_F9,0},{VK_F10,3},true).action==0,"released old binding does not stay latched after rebinding");

    const HWND previous=GetForegroundWindow();
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraGameHotkeyTest";RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"Veyra hotkey test (temporary)",WS_OVERLAPPEDWINDOW,200,150,800,500,nullptr,nullptr,wc.hInstance,nullptr);
    HWND target=CreateWindowW(wc.lpszClassName,L"Veyra geometry test",WS_POPUP,350,280,640,360,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window||!target)return 2;
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);pump(40);
    if(GetForegroundWindow()!=window){const DWORD fg=GetWindowThreadProcessId(GetForegroundWindow(),nullptr),self=GetCurrentThreadId();
        const bool attached=fg&&fg!=self&&AttachThreadInput(fg,self,TRUE);BringWindowToTop(window);SetForegroundWindow(window);if(attached)AttachThreadInput(fg,self,FALSE);}
    check(GetForegroundWindow()==window,"input test owns foreground before sending keys");
    if(GetForegroundWindow()!=window){DestroyWindow(target);DestroyWindow(window);return 1;}
    {
        GamePassthrough geometry;geometry.attach(window);geometry.setTarget(target,MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),false);
        RECT source{};DwmGetWindowAttribute(target,DWMWA_EXTENDED_FRAME_BOUNDS,&source,sizeof(source));POINT origin{};ClientToScreen(window,&origin);
        const auto r=geometry.pictureBounds();
        check(r.left+origin.x==source.left&&r.top+origin.y==source.top&&r.right-r.left==640&&r.bottom-r.top==360,"window picture retains native location and extent");
        SetWindowPos(target,nullptr,420,300,720,400,SWP_NOACTIVATE|SWP_NOZORDER);const auto moved=geometry.pictureBounds();
        check(moved.left-r.left==70&&moved.top-r.top==20&&moved.right-moved.left==720,"window move and resize keep physical coordinates");
    }
    GameHotkeyListener hook;listener=&hook;hook.bindings({VK_F9,0},{VK_F10,3});
    check(hook.start(window),"real WH_KEYBOARD_LL installs on its own message thread");hook.panelOwned(true);
    if(hook.running()){
        check(send({{VK_F9,false},{VK_F9,false},{VK_F9,true}}),"real repeat input delivered");pump(100);
        check(panels==1,"real key repeat emits one panel event");
        hook.panelOwned(false);tap(VK_F9);check(panels==1,"listener leaves panel key to other contexts");hook.panelOwned(true);
        // Raw-input foreground is tested with the real player and a separate
        // process, as in actual game capture. Registering NOLEGACY on this test
        // process changes the listener/recipient itself and is a different case.
        tap(VK_F9);check(panels==2,"panel key responds after returning ownership");
        check(send({{VK_LCONTROL,false},{VK_LMENU,false},{VK_F10,false},{VK_F10,true},{VK_LMENU,true},{VK_LCONTROL,true}}),"custom exit chord delivered");pump(100);
        check(exits==1,"custom Ctrl Alt F10 exits once");
        std::thread inputThread([]{Sleep(100);key(VK_F9);key(VK_F9,true);});Sleep(1500);inputThread.join();pump(100);
        check(panels==3,"keyboard listener survives a busy GUI thread");
        hook.stop();tap(VK_F9);check(panels==3&&!hook.running(),"stopped listener does not consume or trigger keys");
        check(hook.start(window),"listener can reenter after exit");hook.panelOwned(true);tap(VK_F9);check(panels==4,"new session responds to custom key");hook.stop();
    }
    listener=nullptr;quickWindowSequence(target);
    DestroyWindow(target);DestroyWindow(window);if(IsWindow(previous))SetForegroundWindow(previous);
    std::cout<<"TOTAL failures="<<failures<<std::endl;return failures?1:0;
}
