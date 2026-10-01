#pragma once
#include "Theme.h"
#include <functional>
#include <memory>

namespace veyra::ui {
struct SubtitleSettings {
    int pixels=22,margin=0,offset=0,lines=2,font=0;
    bool outline=true,background=false,fitToLines=false;
};
namespace subtitlePanel {
inline HWND window=nullptr;
struct State {
    HFONT font=nullptr;
    SubtitleSettings settings;
    std::function<void(const SubtitleSettings&)> changed;
    bool populating=true;
    UINT dpi=96;
};
inline LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(s));}
    if(!s)return DefWindowProcW(h,msg,w,l);
    if(msg==WM_CREATE){
        titleTheme(h);s->dpi=GetDpiForWindow(h);s->font=makeFont(h);
        auto control=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style,int x,int y,int width,int height){
            HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,dip(h,x),dip(h,y),dip(h,width),dip(h,height),h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(c,WM_SETFONT,WPARAM(s->font),TRUE);themeControl(c);return c;
        };
        auto number=[&](const wchar_t* name,int id,int y,int value,int low,int high){
            control(L"STATIC",name,0,SS_CENTERIMAGE,16,y,214,30);
            HWND edit=control(L"EDIT",std::to_wstring(value).c_str(),id,WS_TABSTOP|ES_RIGHT|ES_AUTOHSCROLL,236,y,102,30);
            SendMessageW(edit,EM_SETLIMITTEXT,6,0);
            HWND spin=control(UPDOWN_CLASSW,L"",id+20,UDS_ALIGNRIGHT|UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_NOTHOUSANDS,0,0,0,0);
            SendMessageW(spin,UDM_SETBUDDY,WPARAM(edit),0);SendMessageW(spin,UDM_SETRANGE32,low,high);SendMessageW(spin,UDM_SETPOS32,0,value);
        };
        number(L"Font size",101,16,s->settings.pixels,16,56);
        number(L"Bottom margin",102,58,s->settings.margin,0,240);
        number(L"Primary subtitle delay (ms)",103,100,s->settings.offset,-30000,30000);
        number(L"Scale target lines (0 = auto)",104,142,s->settings.lines,0,8);
        HWND outline=control(L"BUTTON",L"Outline",105,BS_AUTOCHECKBOX|WS_TABSTOP,16,188,134,30);
        HWND background=control(L"BUTTON",L"Background bar",106,BS_AUTOCHECKBOX|WS_TABSTOP,160,188,178,30);
        SendMessageW(outline,BM_SETCHECK,s->settings.outline?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(background,BM_SETCHECK,s->settings.background?BST_CHECKED:BST_UNCHECKED,0);
        control(L"STATIC",L"Font",0,SS_CENTERIMAGE,16,232,80,30);
        HWND font=control(L"COMBOBOX",L"",107,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,100,232,238,220);
        for(const auto* name:{L"Subtitle's original font",L"SimHei",L"SimSun",L"DengXian",L"Arial",L"Segoe UI"})SendMessageW(font,CB_ADDSTRING,0,LPARAM(name));
        SendMessageW(font,CB_SETCURSEL,s->settings.font,0);
        HWND fit=control(L"BUTTON",L"Auto-shrink font size to fit target lines",108,BS_AUTOCHECKBOX|WS_TABSTOP,16,274,322,30);
        SendMessageW(fit,BM_SETCHECK,s->settings.fitToLines?BST_CHECKED:BST_UNCHECKED,0);
        EnableWindow(GetDlgItem(h,104),s->settings.fitToLines);EnableWindow(GetDlgItem(h,124),s->settings.fitToLines);
        s->populating=false;return 0;
    }
    if(msg==WM_DPICHANGED){
        const UINT dpi=HIWORD(w);const auto* r=reinterpret_cast<const RECT*>(l);
        SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        HFONT font=makeFont(h);
        for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&bounds),2);
            SetWindowPos(child,nullptr,MulDiv(bounds.left,dpi,s->dpi),MulDiv(bounds.top,dpi,s->dpi),MulDiv(bounds.right-bounds.left,dpi,s->dpi),MulDiv(bounds.bottom-bounds.top,dpi,s->dpi),SWP_NOZORDER|SWP_NOACTIVATE);
            SendMessageW(child,WM_SETFONT,WPARAM(font),TRUE);
        }
        DeleteObject(s->font);s->font=font;s->dpi=dpi;return 0;
    }
    if(msg==WM_COMMAND&&!s->populating){
        const int id=LOWORD(w),notification=HIWORD(w);
        if(id==IDCANCEL){DestroyWindow(h);return 0;}
        bool changed=false;
        if(id>=101&&id<=104&&notification==EN_CHANGE){
            wchar_t value[32]{};GetDlgItemTextW(h,id,value,32);wchar_t* end=nullptr;const long n=wcstol(value,&end,10);
            const int low=id==101?16:id==103?-30000:0,high=id==101?56:id==102?240:id==103?30000:8;
            if(end!=value&&!*end&&n>=low&&n<=high){int* field=id==101?&s->settings.pixels:id==102?&s->settings.margin:id==103?&s->settings.offset:&s->settings.lines;*field=int(n);changed=true;}
        }else if(id>=101&&id<=104&&notification==EN_KILLFOCUS){
            const int value=id==101?s->settings.pixels:id==102?s->settings.margin:id==103?s->settings.offset:s->settings.lines;
            s->populating=true;SetDlgItemTextW(h,id,std::to_wstring(value).c_str());s->populating=false;
        }else if((id==105||id==106)&&notification==BN_CLICKED){
            (id==105?s->settings.outline:s->settings.background)=IsDlgButtonChecked(h,id)==BST_CHECKED;changed=true;
        }else if(id==107&&notification==CBN_SELCHANGE){s->settings.font=int(SendDlgItemMessageW(h,id,CB_GETCURSEL,0,0));changed=true;}
        else if(id==108&&notification==BN_CLICKED){s->settings.fitToLines=IsDlgButtonChecked(h,id)==BST_CHECKED;EnableWindow(GetDlgItem(h,104),s->settings.fitToLines);EnableWindow(GetDlgItem(h,124),s->settings.fitToLines);changed=true;}
        if(changed&&s->changed)s->changed(s->settings);
        return 0;
    }
    if(msg==WM_CTLCOLORSTATIC||msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORBTN)return colors(msg,w,l);
    if(msg==WM_CLOSE){DestroyWindow(h);return 0;}
    if(msg==WM_NCDESTROY){DeleteObject(s->font);delete s;SetWindowLongPtrW(h,GWLP_USERDATA,0);window=nullptr;}
    return DefWindowProcW(h,msg,w,l);
}
}
inline void showSubtitleSettings(HWND owner,SubtitleSettings settings,std::function<void(const SubtitleSettings&)> changed){
    if(subtitlePanel::window){ShowWindow(subtitlePanel::window,SW_SHOWNORMAL);SetForegroundWindow(subtitlePanel::window);return;}
    WNDCLASSW wc{};wc.lpfnWndProc=subtitlePanel::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraSubtitleSettings";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto* state=new subtitlePanel::State;state->settings=settings;state->changed=std::move(changed);
    RECT r{0,0,dip(owner,360),dip(owner,340)};AdjustWindowRectExForDpi(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW,layoutDpi(owner));
    RECT p{};GetWindowRect(owner,&p);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    const int width=r.right-r.left,height=r.bottom-r.top;
    const int x=std::clamp(int(p.left)+dip(owner,40),int(monitor.rcWork.left),std::max(int(monitor.rcWork.left),int(monitor.rcWork.right)-width));
    const int y=std::clamp(int(p.top)+dip(owner,80),int(monitor.rcWork.top),std::max(int(monitor.rcWork.top),int(monitor.rcWork.bottom)-height));
    subtitlePanel::window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Subtitle Settings",WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,wc.hInstance,state);
    if(subtitlePanel::window)ShowWindow(subtitlePanel::window,SW_SHOW);
}
inline bool subtitleSettingsDialogMessage(MSG& message){return subtitlePanel::window&&IsDialogMessageW(subtitlePanel::window,&message);}
inline void closeSubtitleSettings(){if(subtitlePanel::window)DestroyWindow(subtitlePanel::window);}
}
