#include "CapturePanel.h"
#include "CaptureConfigBlock.h"
#include "Theme.h"
#include "SettingHelp.h"
namespace veyra::ui {
namespace {
constexpr int BlockTimer=1,ConnectButton=4;
HWND window=nullptr;HFONT font=nullptr;std::function<void(const std::wstring&)> start;
CaptureConfigBlock block;
std::function<bool()> readSdr;std::function<bool(bool)> setSdr;std::function<int()> readAudioIngress;std::function<bool(int)> setAudioIngress;
std::function<bool()> readFlip;std::function<bool(bool)> setFlip;
std::function<int()> readBuffer;std::function<bool(int)> setBuffer;
void connectEnabled(){EnableWindow(GetDlgItem(window,ConnectButton),block.selectionReady());}
void arrange(){RECT r{};GetClientRect(window,&r);const int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window));block.arrange(0,0,width);MoveWindow(GetDlgItem(window,ConnectButton),dip(window,16),dip(window,448),dip(window,width-184),dip(window,36),TRUE);}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);titleTheme(h);
    CaptureConfigBlock::Callbacks callbacks;
    callbacks.readSdr=readSdr;callbacks.setSdr=setSdr;callbacks.readAudioIngress=readAudioIngress;callbacks.setAudioIngress=setAudioIngress;
    callbacks.readFlip=readFlip;callbacks.setFlip=setFlip;callbacks.readBuffer=readBuffer;callbacks.setBuffer=setBuffer;
    callbacks.stateChanged=connectEnabled;
    block.create(h,font,0,BlockTimer,std::move(callbacks));
    auto connect=CreateWindowExW(0,L"BUTTON",L"连接并开始观看",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,0,0,1,1,h,HMENU(INT_PTR(ConnectButton)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(connect,WM_SETFONT,WPARAM(font),TRUE);themeControl(connect);marked(connect);EnableWindow(connect,FALSE);
    installDialogHelp(h,{{ConnectButton,L"按当前格式连接采集卡，并应用当前增强设置。"}});
    auto help=block.helpEntries();installDialogHelp(h,{help.begin(),help.end()});
    arrange();return 0;}
case WM_TIMER:if(wp==BlockTimer)block.poll();return 0;
case WM_COMMAND:
    if(LOWORD(wp)==ConnectButton&&HIWORD(wp)==BN_CLICKED){
        const auto path=block.capturePath();
        if(!path.empty()){start(path);DestroyWindow(h);}
        return 0;
    }
    block.handleCommand(wp,lp);return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_SIZE:arrange();return 0;
case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);auto old=font;font=makeFont(h);EnumChildWindows(h,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();return 0;}
case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:block.destroy();KillTimer(h,BlockTimer);DeleteObject(font);window=nullptr;return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showCapturePanel(HWND parent,std::function<void(const std::wstring&)> callback,std::function<bool()> read,std::function<bool(bool)> write,std::function<int()> readIngress,std::function<bool(int)> writeIngress,std::function<bool()> readFlipped,std::function<bool(bool)> writeFlipped,std::function<int()> readBuffered,std::function<bool(int)> writeBuffered){start=std::move(callback);readSdr=std::move(read);setSdr=std::move(write);readAudioIngress=std::move(readIngress);setAudioIngress=std::move(writeIngress);readFlip=std::move(readFlipped);setFlip=std::move(writeFlipped);readBuffer=std::move(readBuffered);setBuffer=std::move(writeBuffered);if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"采集卡 · 连接设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,560),dip(parent,680),parent,nullptr,wc.hInstance,nullptr);}
}
